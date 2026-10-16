#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common/models.h"
#include "common/protocol.h"
#include "common/network.h"

static void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

static const char *get_role_label(int role) {
    switch (role) {
        case ROLE_CUSTOMER: return "Customer";
        case ROLE_EMPLOYEE: return "Bank Employee";
        case ROLE_MANAGER:  return "Bank Manager";
        case ROLE_ADMIN:    return "Administrator";
        default:            return "Unknown";
    }
}

static void show_about_accounts(void) {
    printf("\n======================================================\n");
    printf("   Pre-seeded Test Accounts (Default Credentials)     \n");
    printf("======================================================\n");
    printf(" 1. Administrator:  Username='admin'     Password='adminpassword'\n");
    printf(" 2. Bank Manager:   Username='manager1'  Password='managerpassword'\n");
    printf(" 3. Bank Employee:  Username='employee1' Password='employeepassword'\n");
    printf(" 4. Customer Alice: Username='alice'     Password='alicepassword' (Acc: 2001, $15k)\n");
    printf(" 5. Customer Bob:   Username='bob'       Password='bobpassword'   (Acc: 2002, $8k)\n");
    printf("======================================================\n\n");
}

static void handle_user_session(int sock, AuthSuccessPayload *auth) {
    RequestPacket req;
    ResponsePacket res;
    int choice = 0;

    while (1) {
        printf("\n======================================================\n");
        printf("   USER SESSION: %s\n", auth->customer_name);
        printf("   User ID: %-6d | Username: %-12s | Role: %s\n",
               auth->user_id, auth->username, get_role_label(auth->role));
        if (auth->role == ROLE_CUSTOMER && auth->account_id > 0) {
            printf("   Primary Account ID: %-6d | Available Balance: $%.2f\n",
                   auth->account_id, auth->balance);
        }
        printf("======================================================\n");
        printf(" 1. Change Password\n");
        printf(" 2. View Session Details & Role Information\n");
        printf(" 3. Send Handshake Probe (Test Worker Connection)\n");
        printf(" 4. Logout\n");
        printf("Enter your choice (1-4): ");

        if (scanf("%d", &choice) != 1) {
            clear_input_buffer();
            break;
        }
        clear_input_buffer();

        if (choice == 1) {
            char old_pwd[64] = {0};
            char new_pwd[64] = {0};
            char confirm_pwd[64] = {0};

            printf("\n--- Change Password ---\n");
            printf("Enter current password: ");
            if (scanf("%63s", old_pwd) != 1) { clear_input_buffer(); continue; }
            printf("Enter new password (min 4 chars): ");
            if (scanf("%63s", new_pwd) != 1) { clear_input_buffer(); continue; }
            printf("Confirm new password: ");
            if (scanf("%63s", confirm_pwd) != 1) { clear_input_buffer(); continue; }
            clear_input_buffer();

            if (strcmp(new_pwd, confirm_pwd) != 0) {
                printf("\n[-] Error: New password and confirmation do not match!\n");
                continue;
            }

            ChangePasswordPayload cp;
            memset(&cp, 0, sizeof(cp));
            strncpy(cp.old_password, old_pwd, sizeof(cp.old_password) - 1);
            strncpy(cp.new_password, new_pwd, sizeof(cp.new_password) - 1);

            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CHANGE_PASSWORD;
            req.user_id = auth->user_id;
            req.role = auth->role;
            memcpy(req.payload, &cp, sizeof(cp));

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Network error communicating with server.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                printf("\n[+] SUCCESS: %s\n", res.message);
            } else {
                printf("\n[-] FAILED [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 2) {
            printf("\n======================================================\n");
            printf("               SESSION & ROLE DETAILS                 \n");
            printf("======================================================\n");
            printf(" User ID:        %d\n", auth->user_id);
            printf(" Username:       %s\n", auth->username);
            printf(" Display Name:   %s\n", auth->customer_name);
            printf(" Role:           %s (Level %d)\n", get_role_label(auth->role), auth->role);
            if (auth->role == ROLE_CUSTOMER) {
                printf(" Account Number: %d\n", auth->account_id);
                printf(" Balance:        $%.2f\n", auth->balance);
                printf("\n [*] Note: Customer operations (Deposit, Withdraw, Transfer,\n");
                printf("     Passbook, Loans, Feedback) will unlock in Days 6-8.\n");
            } else if (auth->role == ROLE_EMPLOYEE) {
                printf("\n [*] Note: Employee operations (Add Customer, Modify Details,\n");
                printf("     Approve/Reject Loans) will unlock in Day 9.\n");
            } else if (auth->role == ROLE_MANAGER) {
                printf("\n [*] Note: Manager operations (Activate/Deactivate Accounts,\n");
                printf("     Assign Loans, Review Feedback) will unlock in Day 10.\n");
            } else if (auth->role == ROLE_ADMIN) {
                printf("\n [*] Note: Administrator operations (Add Employees, Manage Roles,\n");
                printf("     Full System Oversight) will unlock in Day 11.\n");
            }
            printf("======================================================\n");

        } else if (choice == 3) {
            memset(&req, 0, sizeof(req));
            req.opcode = 100;
            req.user_id = auth->user_id;
            req.role = auth->role;
            snprintf(req.payload, sizeof(req.payload), "Ping from user '%s'", auth->username);

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Server disconnected.\n");
                break;
            }
            printf("\n[+] SERVER RESPONSE [%d]: %s\n", res.status_code, res.message);
            if (res.data_len > 0) {
                printf("    Data: %s\n", res.payload);
            }

        } else if (choice == 4) {
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_LOGOUT;
            req.user_id = auth->user_id;

            if (send_request(sock, &req) == 0 && recv_response(sock, &res) == 0) {
                printf("\n[+] %s\n", res.message);
            } else {
                printf("\n[*] Logged out locally.\n");
            }
            break;
        } else {
            printf("\n[-] Invalid option. Please enter 1-4.\n");
        }
    }
}

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    printf("=====================================================\n");
    printf("   Banking Management System - Client Terminal       \n");
    printf("=====================================================\n");
    printf("[*] Connecting to Banking Server at %s:%d...\n", host, port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed. Is the banking server running?");
        close(sock);
        return 1;
    }

    printf("[+] Successfully connected to Banking Server!\n\n");

    int choice = 0;
    while (1) {
        printf("=====================================================\n");
        printf("            MAIN MENU - CLIENT TERMINAL              \n");
        printf("=====================================================\n");
        printf(" 1. User Login (Customer, Employee, Manager, Admin) \n");
        printf(" 2. View Pre-seeded Test Accounts                   \n");
        printf(" 3. Disconnect & Exit                               \n");
        printf("=====================================================\n");
        printf("Enter choice (1-3): ");

        if (scanf("%d", &choice) != 1) {
            clear_input_buffer();
            break;
        }
        clear_input_buffer();

        if (choice == 1) {
            LoginPayload login;
            memset(&login, 0, sizeof(login));

            printf("\n--- User Login ---\n");
            printf("Enter Username: ");
            if (scanf("%63s", login.username) != 1) { clear_input_buffer(); continue; }
            printf("Enter Password: ");
            if (scanf("%63s", login.password) != 1) { clear_input_buffer(); continue; }
            clear_input_buffer();

            RequestPacket req;
            ResponsePacket res;
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_LOGIN;
            memcpy(req.payload, &login, sizeof(login));

            printf("\n[*] Sending login request to server...\n");
            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("[-] Error communicating with server.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                AuthSuccessPayload auth;
                memcpy(&auth, res.payload, sizeof(auth));
                printf("[+] %s\n", res.message);
                handle_user_session(sock, &auth);
            } else {
                printf("\n[-] Login Failed [%d]: %s\n\n", res.status_code, res.message);
            }

        } else if (choice == 2) {
            show_about_accounts();
        } else if (choice == 3) {
            RequestPacket req;
            ResponsePacket res;
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_EXIT;
            send_request(sock, &req);
            recv_response(sock, &res);
            printf("\n%s\n", res.message);
            break;
        } else {
            printf("\n[-] Invalid choice. Please enter 1-3.\n");
        }
    }

    close(sock);
    printf("[*] Client closed connection. Goodbye!\n");
    return 0;
}
