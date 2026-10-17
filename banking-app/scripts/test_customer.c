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
#include "server/customer_ops.h"
#include "server/db.h"
#include "server/lock.h"
#include "server/connection_handler.h"

#define TEST_CUST_PORT 9299

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
    printf("   Banking Management System - Customer Ops Test     \n");
    printf("=====================================================\n\n");

    db_init();

    int alice_uid = 1004;
    int alice_acc = 2001;

    /* =======================================================
     * PHASE 1: Direct Service Logic Verification
     * ======================================================= */
    printf("--- [PHASE 1] Direct Service Unit Tests ---\n");

    /* Test 1.1: Balance Query */
    BalancePayload bal;
    char msg[256] = {0};
    int status = customer_get_balance(alice_uid, alice_acc, &bal, msg, sizeof(msg));
    if (status == RESP_OK && bal.balance > 0.0) {
        printf("[+] TEST 1.1 PASSED: Retrieved balance $%.2f for Alice (Acc #%d)\n",
               bal.balance, bal.account_id);
    } else {
        fprintf(stderr, "[-] TEST 1.1 FAILED: Could not retrieve balance (status=%d, msg='%s')\n", status, msg);
        return 1;
    }
    double baseline_balance = bal.balance;

    /* Test 1.2: Deposit Funds */
    TransactionResultPayload tr;
    status = customer_deposit(alice_uid, alice_acc, 5000.0, &tr, msg, sizeof(msg));
    if (status == RESP_OK && tr.balance_after == (baseline_balance + 5000.0)) {
        printf("[+] TEST 1.2 PASSED: Deposited $5,000.00. New balance: $%.2f (Txn ID: %d)\n",
               tr.balance_after, tr.transaction_id);
    } else {
        fprintf(stderr, "[-] TEST 1.2 FAILED: Deposit failed (status=%d, balance=%.2f)\n",
                status, tr.balance_after);
        return 1;
    }

    /* Test 1.3: Withdraw Funds */
    status = customer_withdraw(alice_uid, alice_acc, 3000.0, &tr, msg, sizeof(msg));
    if (status == RESP_OK && tr.balance_after == (baseline_balance + 2000.0)) {
        printf("[+] TEST 1.3 PASSED: Withdrew $3,000.00. Remaining balance: $%.2f (Txn ID: %d)\n",
               tr.balance_after, tr.transaction_id);
    } else {
        fprintf(stderr, "[-] TEST 1.3 FAILED: Withdrawal failed (status=%d, balance=%.2f)\n",
                status, tr.balance_after);
        return 1;
    }

    /* Test 1.4: Insufficient Funds (Overdraft Attempt) */
    status = customer_withdraw(alice_uid, alice_acc, 999999.0, &tr, msg, sizeof(msg));
    if (status == RESP_ERR_INSUFFICIENT_FUNDS) {
        printf("[+] TEST 1.4 PASSED: Overdraft of $999,999.00 correctly rejected with RESP_ERR_INSUFFICIENT_FUNDS (422).\n");
    } else {
        fprintf(stderr, "[-] TEST 1.4 FAILED: Expected 422 Insufficient Funds, got %d\n", status);
        return 1;
    }

    /* Test 1.5: Non-positive Amount Validation */
    status = customer_deposit(alice_uid, alice_acc, -100.0, &tr, msg, sizeof(msg));
    if (status == RESP_ERR_BAD_REQUEST) {
        printf("[+] TEST 1.5 PASSED: Negative deposit amount rejected with RESP_ERR_BAD_REQUEST (400).\n");
    } else {
        fprintf(stderr, "[-] TEST 1.5 FAILED: Expected 400, got %d\n", status);
        return 1;
    }

    /* Test 1.6: Unauthorized Access (Bob trying to view/withdraw from Alice's account) */
    int bob_uid = 1005;
    status = customer_get_balance(bob_uid, alice_acc, &bal, msg, sizeof(msg));
    if (status == RESP_ERR_UNAUTHORIZED) {
        printf("[+] TEST 1.6 PASSED: Cross-user account access rejected with RESP_ERR_UNAUTHORIZED (403).\n");
    } else {
        fprintf(stderr, "[-] TEST 1.6 FAILED: Expected 403, got %d\n", status);
        return 1;
    }

    /* =======================================================
     * PHASE 2: Concurrency & Record Locking Race-Condition Test
     * ======================================================= */
    printf("\n--- [PHASE 2] Concurrency & ACID Serialization Test ---\n");
    customer_get_balance(alice_uid, alice_acc, &bal, NULL, 0);
    double pre_concurrency_balance = bal.balance;
    printf("[*] Starting concurrent test with account balance: $%.2f\n", pre_concurrency_balance);

    int num_txns = 50;
    double txn_amt = 10.0;

    pid_t p1 = fork();
    if (p1 == 0) {
        /* Worker 1: 50 deposits of $10 */
        for (int i = 0; i < num_txns; ++i) {
            TransactionResultPayload res_p;
            customer_deposit(alice_uid, alice_acc, txn_amt, &res_p, NULL, 0);
            usleep(1000); /* 1ms delay */
        }
        exit(0);
    }

    pid_t p2 = fork();
    if (p2 == 0) {
        /* Worker 2: 50 withdrawals of $10 */
        for (int i = 0; i < num_txns; ++i) {
            TransactionResultPayload res_p;
            customer_withdraw(alice_uid, alice_acc, txn_amt, &res_p, NULL, 0);
            usleep(1000); /* 1ms delay */
        }
        exit(0);
    }

    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);

    customer_get_balance(alice_uid, alice_acc, &bal, NULL, 0);
    double post_concurrency_balance = bal.balance;

    if (post_concurrency_balance == pre_concurrency_balance) {
        printf("[+] TEST 2.1 PASSED: Strict fcntl exclusive record locks serialized 100 concurrent ops!\n");
        printf("    Balance precisely preserved: $%.2f == $%.2f (Zero race conditions)\n",
               post_concurrency_balance, pre_concurrency_balance);
    } else {
        fprintf(stderr, "[-] TEST 2.1 FAILED: Lost update detected! Expected $%.2f, got $%.2f\n",
                pre_concurrency_balance, post_concurrency_balance);
        return 1;
    }

    /* =======================================================
     * PHASE 3: End-to-End Client-Server TCP Protocol Verification
     * ======================================================= */
    printf("\n--- [PHASE 3] End-to-End TCP Client-Server Protocol Test ---\n");

    /* Fork test server */
    pid_t server_pid = fork();
    if (server_pid == 0) {
        int server_sock = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = INADDR_ANY;
        saddr.sin_port = htons(TEST_CUST_PORT);

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

    int sock = connect_to_test_server(TEST_CUST_PORT);
    if (sock < 0) {
        fprintf(stderr, "[-] Failed to connect to test server.\n");
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    /* 3.1: Login as Alice */
    RequestPacket req;
    ResponsePacket res;
    memset(&req, 0, sizeof(req));
    req.opcode = REQ_LOGIN;
    LoginPayload login_alice = {"alice", "alicepassword"};
    memcpy(req.payload, &login_alice, sizeof(login_alice));
    send_request(sock, &req);
    recv_response(sock, &res);

    if (res.status_code == RESP_OK) {
        printf("[+] TEST 3.1 PASSED: Client logged in over TCP as Alice.\n");
    } else {
        fprintf(stderr, "[-] TEST 3.1 FAILED: Login failed (%d: %s)\n", res.status_code, res.message);
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    /* 3.2: View Balance via TCP */
    memset(&req, 0, sizeof(req));
    req.opcode = REQ_CUST_VIEW_BALANCE;
    req.user_id = alice_uid;
    req.role = ROLE_CUSTOMER;
    send_request(sock, &req);
    recv_response(sock, &res);

    if (res.status_code == RESP_OK) {
        BalancePayload b;
        memcpy(&b, res.payload, sizeof(b));
        printf("[+] TEST 3.2 PASSED: View Balance over TCP returned $%.2f for Acc #%d\n",
               b.balance, b.account_id);
    } else {
        fprintf(stderr, "[-] TEST 3.2 FAILED: View balance over TCP failed.\n");
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    /* 3.3: Deposit via TCP */
    memset(&req, 0, sizeof(req));
    req.opcode = REQ_CUST_DEPOSIT;
    req.user_id = alice_uid;
    req.role = ROLE_CUSTOMER;
    AmountPayload ap_dep = {1200.0};
    memcpy(req.payload, &ap_dep, sizeof(ap_dep));
    send_request(sock, &req);
    recv_response(sock, &res);

    if (res.status_code == RESP_OK) {
        TransactionResultPayload tr_tcp;
        memcpy(&tr_tcp, res.payload, sizeof(tr_tcp));
        printf("[+] TEST 3.3 PASSED: Deposit $1,200.00 over TCP confirmed! New Balance: $%.2f\n",
               tr_tcp.balance_after);
    } else {
        fprintf(stderr, "[-] TEST 3.3 FAILED: Deposit over TCP failed.\n");
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    /* 3.4: Withdraw via TCP */
    memset(&req, 0, sizeof(req));
    req.opcode = REQ_CUST_WITHDRAW;
    req.user_id = alice_uid;
    req.role = ROLE_CUSTOMER;
    AmountPayload ap_with = {700.0};
    memcpy(req.payload, &ap_with, sizeof(ap_with));
    send_request(sock, &req);
    recv_response(sock, &res);

    if (res.status_code == RESP_OK) {
        TransactionResultPayload tr_tcp;
        memcpy(&tr_tcp, res.payload, sizeof(tr_tcp));
        printf("[+] TEST 3.4 PASSED: Withdrawal $700.00 over TCP confirmed! Remaining: $%.2f\n",
               tr_tcp.balance_after);
    } else {
        fprintf(stderr, "[-] TEST 3.4 FAILED: Withdrawal over TCP failed.\n");
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    /* Disconnect and cleanup */
    memset(&req, 0, sizeof(req));
    req.opcode = REQ_EXIT;
    send_request(sock, &req);
    close(sock);

    kill(server_pid, SIGKILL);
    waitpid(server_pid, NULL, 0);

    printf("\n=====================================================\n");
    printf("   All Day 6 Customer Ops Tests Passed! (11/11)      \n");
    printf("=====================================================\n");

    return 0;
}
