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

/* ==================== CUSTOMER MENU (DAY 6) ==================== */
static void handle_customer_session(int sock, AuthSuccessPayload *auth) {
    RequestPacket req;
    ResponsePacket res;
    int choice = 0;

    while (1) {
        printf("\n======================================================\n");
        printf("   CUSTOMER PORTAL: %s\n", auth->customer_name);
        printf("   Account #: %-8d | User ID: %-6d | Balance: $%.2f\n",
               auth->account_id, auth->user_id, auth->balance);
        printf("======================================================\n");
        printf(" 1. View Account Balance (Live Query)\n");
        printf(" 2. Deposit Funds\n");
        printf(" 3. Withdraw Funds\n");
        printf(" 4. Change Password\n");
        printf(" 5. View Account & Session Details\n");
        printf(" 6. Send Handshake Probe (Ping Server)\n");
        printf(" 7. Logout\n");
        printf("Enter your choice (1-7): ");

        if (scanf("%d", &choice) != 1) {
            clear_input_buffer();
            break;
        }
        clear_input_buffer();

        if (choice == 1) {
            /* Live Balance Query */
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CUST_VIEW_BALANCE;
            req.user_id = auth->user_id;
            req.role = auth->role;

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Error communicating with server.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                BalancePayload bal;
                memcpy(&bal, res.payload, sizeof(bal));
                auth->balance = bal.balance; /* update local cache */

                printf("\n------------------------------------------------------\n");
                printf("                ACCOUNT BALANCE DETAILS               \n");
                printf("------------------------------------------------------\n");
                printf(" Account Number:    %d\n", bal.account_id);
                printf(" Account Holder:    %s\n", bal.customer_name);
                printf(" Account Status:    %s\n", bal.is_active ? "ACTIVE" : "FROZEN");
                printf(" Available Balance: $%.2f\n", bal.balance);
                printf("------------------------------------------------------\n");
            } else {
                printf("\n[-] Failed to retrieve balance [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 2) {
            /* Deposit Funds */
            double amount = 0.0;
            printf("\n--- Deposit Funds ---\n");
            printf("Enter amount to deposit ($): ");
            if (scanf("%lf", &amount) != 1) {
                clear_input_buffer();
                printf("[-] Invalid amount entered.\n");
                continue;
            }
            clear_input_buffer();

            if (amount <= 0.0) {
                printf("[-] Deposit amount must be greater than zero.\n");
                continue;
            }

            AmountPayload ap;
            ap.amount = amount;

            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CUST_DEPOSIT;
            req.user_id = auth->user_id;
            req.role = auth->role;
            memcpy(req.payload, &ap, sizeof(ap));

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Network error during deposit.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                TransactionResultPayload tr;
                memcpy(&tr, res.payload, sizeof(tr));
                auth->balance = tr.balance_after; /* update cached balance */

                printf("\n======================================================\n");
                printf("   [+] DEPOSIT SUCCESSFUL!\n");
                printf("   Transaction ID:   %d\n", tr.transaction_id);
                printf("   Credited Amount:  $%.2f\n", tr.amount);
                printf("   Updated Balance:  $%.2f\n", tr.balance_after);
                printf("======================================================\n");
            } else {
                printf("\n[-] Deposit Failed [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 3) {
            /* Withdraw Funds */
            double amount = 0.0;
            printf("\n--- Withdraw Funds ---\n");
            printf("Enter amount to withdraw ($): ");
            if (scanf("%lf", &amount) != 1) {
                clear_input_buffer();
                printf("[-] Invalid amount entered.\n");
                continue;
            }
            clear_input_buffer();

            if (amount <= 0.0) {
                printf("[-] Withdrawal amount must be greater than zero.\n");
                continue;
            }

            AmountPayload ap;
            ap.amount = amount;

            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CUST_WITHDRAW;
            req.user_id = auth->user_id;
            req.role = auth->role;
            memcpy(req.payload, &ap, sizeof(ap));

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Network error during withdrawal.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                TransactionResultPayload tr;
                memcpy(&tr, res.payload, sizeof(tr));
                auth->balance = tr.balance_after; /* update cached balance */

                printf("\n======================================================\n");
                printf("   [+] WITHDRAWAL SUCCESSFUL!\n");
                printf("   Transaction ID:   %d\n", tr.transaction_id);
                printf("   Debited Amount:   $%.2f\n", tr.amount);
                printf("   Remaining Balance: $%.2f\n", tr.balance_after);
                printf("======================================================\n");
            } else {
                printf("\n[-] Withdrawal Failed [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 4) {
            /* Change Password */
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

        } else if (choice == 5) {
            /* Session & Account Details */
            printf("\n======================================================\n");
            printf("               ACCOUNT & SESSION DETAILS              \n");
            printf("======================================================\n");
            printf(" User ID:        %d\n", auth->user_id);
            printf(" Username:       %s\n", auth->username);
            printf(" Customer Name:  %s\n", auth->customer_name);
            printf(" Role:           Customer\n");
            printf(" Primary Acc #:  %d\n", auth->account_id);
            printf(" Cached Balance: $%.2f\n", auth->balance);
            printf("\n [*] Note: Atomic Transfers & Passbook will unlock in Day 7.\n");
            printf(" [*] Note: Loan Application & Feedback will unlock in Day 8.\n");
            printf("======================================================\n");

        } else if (choice == 6) {
            /* Handshake Probe */
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

        } else if (choice == 7) {
            /* Logout */
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
            printf("\n[-] Invalid option. Please enter 1-7.\n");
        }
    }
}

/* ==================== STAFF SESSION (ADMIN, MANAGER, EMPLOYEE) ==================== */
static void handle_staff_session(int sock, AuthSuccessPayload *auth) {
    RequestPacket req;
    ResponsePacket res;
    int choice = 0;

    while (1) {
        printf("\n======================================================\n");
        printf("   STAFF SESSION: %s\n", auth->username);
        printf("   User ID: %-6d | Role: %s (Level %d)\n",
               auth->user_id, get_role_label(auth->role), auth->role);
        printf("======================================================\n");
        printf(" 1. Change Password\n");
        printf(" 2. View Staff Session & Role Information\n");
        printf(" 3. Send Handshake Probe (Test Server Ping)\n");
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
            printf("               STAFF ROLE DETAILS                     \n");
            printf("======================================================\n");
            printf(" User ID:        %d\n", auth->user_id);
            printf(" Username:       %s\n", auth->username);
            printf(" Role:           %s\n", get_role_label(auth->role));
            if (auth->role == ROLE_EMPLOYEE) {
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
            snprintf(req.payload, sizeof(req.payload), "Ping from staff '%s'", auth->username);

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Server disconnected.\n");
                break;
            }
            printf("\n[+] SERVER RESPONSE [%d]: %s\n", res.status_code, res.message);

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

                if (auth.role == ROLE_CUSTOMER) {
                    handle_customer_session(sock, &auth);
                } else {
                    handle_staff_session(sock, &auth);
                }
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
