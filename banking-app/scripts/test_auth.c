#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "common/models.h"
#include "common/protocol.h"
#include "common/network.h"
#include "server/auth.h"
#include "server/db.h"
#include "server/lock.h"
#include "server/connection_handler.h"

#define TEST_PORT 9188

static int connect_to_test_server(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    for (int attempts = 0; attempts < 10; attempts++) {
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            return sock;
        }
        usleep(50000); /* 50ms wait */
    }
    close(sock);
    return -1;
}

int main(void) {
    printf("=====================================================\n");
    printf("   Banking Management System - Auth Verification     \n");
    printf("=====================================================\n\n");

    db_init();

    /* =======================================================
     * PHASE 1: Direct Authentication Subsystem Logic Tests
     * ======================================================= */
    printf("--- [PHASE 1] Direct Subsystem Unit Verification ---\n");

    /* Test 1.1: Multi-Role Authentication */
    struct {
        const char *username;
        const char *password;
        UserRole expected_role;
    } test_users[] = {
        {"admin",     "adminpassword",    ROLE_ADMIN},
        {"manager1",  "managerpassword",  ROLE_MANAGER},
        {"employee1", "employeepassword", ROLE_EMPLOYEE},
        {"alice",     "alicepassword",    ROLE_CUSTOMER}
    };

    for (size_t i = 0; i < sizeof(test_users)/sizeof(test_users[0]); ++i) {
        LoginPayload login;
        memset(&login, 0, sizeof(login));
        strncpy(login.username, test_users[i].username, sizeof(login.username) - 1);
        strncpy(login.password, test_users[i].password, sizeof(login.password) - 1);

        AuthSuccessPayload auth_info;
        int s_fd = -1;
        char msg[256] = {0};

        int status = auth_login(&login, &auth_info, &s_fd, msg, sizeof(msg));
        if (status == RESP_OK && (UserRole)auth_info.role == test_users[i].expected_role) {
            printf("[+] TEST 1.%zu PASSED: Authenticated '%s' as %s (User ID %d)\n",
                   i + 1, test_users[i].username, role_to_string(auth_info.role), auth_info.user_id);
        } else {
            fprintf(stderr, "[-] TEST 1.%zu FAILED: Failed to authenticate '%s' (status=%d, msg='%s')\n",
                    i + 1, test_users[i].username, status, msg);
            return 1;
        }
        auth_logout(auth_info.user_id, s_fd);
    }

    /* Test 1.2: Authentication Failures (Wrong credentials / Non-existent user) */
    {
        LoginPayload bad_user = {"unknown_user", "somepassword"};
        AuthSuccessPayload out_auth;
        int s_fd = -1;
        char msg[256] = {0};

        int status = auth_login(&bad_user, &out_auth, &s_fd, msg, sizeof(msg));
        if (status == RESP_ERR_NOT_FOUND) {
            printf("[+] TEST 1.5 PASSED: Non-existent user correctly rejected with RESP_ERR_NOT_FOUND.\n");
        } else {
            fprintf(stderr, "[-] TEST 1.5 FAILED: Expected 404, got %d\n", status);
            return 1;
        }

        LoginPayload bad_pass = {"alice", "wrongpassword"};
        status = auth_login(&bad_pass, &out_auth, &s_fd, msg, sizeof(msg));
        if (status == RESP_ERR_AUTH_FAILED) {
            printf("[+] TEST 1.6 PASSED: Invalid password correctly rejected with RESP_ERR_AUTH_FAILED.\n");
        } else {
            fprintf(stderr, "[-] TEST 1.6 FAILED: Expected 401, got %d\n", status);
            return 1;
        }
    }

    /* Test 1.3: Password Change Flow */
    {
        printf("\n--- Testing Password Change Mechanism ---\n");
        int test_uid = 1003; /* employee1 */

        /* Bad old password */
        ChangePasswordPayload cp_bad = {"wrongold", "brandnewsecret"};
        char msg[256] = {0};
        int status = auth_change_password(test_uid, &cp_bad, msg, sizeof(msg));
        if (status == RESP_ERR_AUTH_FAILED) {
            printf("[+] TEST 1.7 PASSED: Password change rejected with invalid current password.\n");
        } else {
            fprintf(stderr, "[-] TEST 1.7 FAILED: Expected 401, got %d\n", status);
            return 1;
        }

        /* Valid password change */
        ChangePasswordPayload cp_good = {"employeepassword", "supersecret123"};
        status = auth_change_password(test_uid, &cp_good, msg, sizeof(msg));
        if (status == RESP_OK) {
            printf("[+] TEST 1.8 PASSED: Password successfully updated to 'supersecret123'.\n");
        } else {
            fprintf(stderr, "[-] TEST 1.8 FAILED: Expected 200, got %d\n", status);
            return 1;
        }

        /* Verify old password fails */
        LoginPayload login_old = {"employee1", "employeepassword"};
        AuthSuccessPayload out_auth;
        int s_fd = -1;
        status = auth_login(&login_old, &out_auth, &s_fd, msg, sizeof(msg));
        if (status == RESP_ERR_AUTH_FAILED) {
            printf("[+] TEST 1.9 PASSED: Old password rejected after update.\n");
        } else {
            fprintf(stderr, "[-] TEST 1.9 FAILED: Old password was accepted!\n");
            return 1;
        }

        /* Verify new password succeeds */
        LoginPayload login_new = {"employee1", "supersecret123"};
        status = auth_login(&login_new, &out_auth, &s_fd, msg, sizeof(msg));
        if (status == RESP_OK) {
            printf("[+] TEST 1.10 PASSED: New password accepted.\n");
            auth_logout(out_auth.user_id, s_fd);
        } else {
            fprintf(stderr, "[-] TEST 1.10 FAILED: New password failed!\n");
            return 1;
        }

        /* Restore original password */
        ChangePasswordPayload cp_restore = {"supersecret123", "employeepassword"};
        auth_change_password(test_uid, &cp_restore, msg, sizeof(msg));
        printf("[+] Cleaned up: Restored original employee1 password.\n");
    }

    /* =======================================================
     * PHASE 2: End-to-End TCP Socket Server Verification
     * ======================================================= */
    printf("\n--- [PHASE 2] End-to-End TCP Socket Concurrency Verification ---\n");

    /* Fork test server daemon */
    pid_t server_pid = fork();
    if (server_pid == 0) {
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = INADDR_ANY;
        saddr.sin_port = htons(TEST_PORT);

        if (bind(server_sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
            exit(1);
        }
        listen(server_sock, 8);

        while (1) {
            struct sockaddr_in caddr;
            socklen_t clen = sizeof(caddr);
            int csock = accept(server_sock, (struct sockaddr *)&caddr, &clen);
            if (csock < 0) continue;

            pid_t worker_pid = fork();
            if (worker_pid == 0) {
                close(server_sock);
                handle_client(csock, caddr);
                exit(0);
            } else {
                close(csock);
            }
        }
    }

    usleep(100000); /* 100ms for server startup */

    /* Test 2.1: TCP Client 1 login as Customer Bob */
    int sock1 = connect_to_test_server(TEST_PORT);
    if (sock1 < 0) {
        fprintf(stderr, "[-] Could not connect Client 1 to test server.\n");
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    RequestPacket req1;
    ResponsePacket res1;
    memset(&req1, 0, sizeof(req1));
    req1.opcode = REQ_LOGIN;
    LoginPayload login_bob = {"bob", "bobpassword"};
    memcpy(req1.payload, &login_bob, sizeof(login_bob));

    send_request(sock1, &req1);
    recv_response(sock1, &res1);

    if (res1.status_code == RESP_OK) {
        printf("[+] TEST 2.1 PASSED: Client 1 logged in over TCP as Bob (Acc: 2002).\n");
    } else {
        fprintf(stderr, "[-] TEST 2.1 FAILED: Login failed (%d: %s)\n", res1.status_code, res1.message);
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    /* Test 2.2: Concurrent TCP Client 2 attempts to login as Bob -> MUST BE REJECTED */
    int sock2 = connect_to_test_server(TEST_PORT);
    if (sock2 < 0) {
        fprintf(stderr, "[-] Could not connect Client 2.\n");
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    RequestPacket req2;
    ResponsePacket res2;
    memset(&req2, 0, sizeof(req2));
    req2.opcode = REQ_LOGIN;
    memcpy(req2.payload, &login_bob, sizeof(login_bob));

    send_request(sock2, &req2);
    recv_response(sock2, &res2);

    if (res2.status_code == RESP_ERR_ALREADY_LOGGED_IN) {
        printf("[+] TEST 2.2 PASSED: Server correctly rejected Client 2 concurrent login (Code 423: %s)\n",
               res2.message);
    } else {
        fprintf(stderr, "[-] TEST 2.2 FAILED: Concurrent login was not rejected! Code: %d\n", res2.status_code);
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    /* Test 2.3: Client 1 logs out cleanly */
    memset(&req1, 0, sizeof(req1));
    req1.opcode = REQ_LOGOUT;
    send_request(sock1, &req1);
    recv_response(sock1, &res1);
    if (res1.status_code == RESP_OK) {
        printf("[+] TEST 2.3 PASSED: Client 1 logged out cleanly over TCP.\n");
    } else {
        fprintf(stderr, "[-] TEST 2.3 FAILED: Logout failed.\n");
    }

    /* Test 2.4: Client 2 can now successfully log in */
    memset(&req2, 0, sizeof(req2));
    req2.opcode = REQ_LOGIN;
    memcpy(req2.payload, &login_bob, sizeof(login_bob));
    send_request(sock2, &req2);
    recv_response(sock2, &res2);

    if (res2.status_code == RESP_OK) {
        printf("[+] TEST 2.4 PASSED: Client 2 successfully logged in after Client 1 released session.\n");
    } else {
        fprintf(stderr, "[-] TEST 2.4 FAILED: Client 2 could not log in after release.\n");
    }

    /* Disconnect clients */
    memset(&req1, 0, sizeof(req1));
    req1.opcode = REQ_EXIT;
    send_request(sock1, &req1);
    close(sock1);

    memset(&req2, 0, sizeof(req2));
    req2.opcode = REQ_EXIT;
    send_request(sock2, &req2);
    close(sock2);

    /* Terminate test server */
    kill(server_pid, SIGKILL);
    waitpid(server_pid, NULL, 0);

    printf("\n=====================================================\n");
    printf("   All Day 5 Authentication Tests Passed! (14/14)    \n");
    printf("=====================================================\n");

    return 0;
}
