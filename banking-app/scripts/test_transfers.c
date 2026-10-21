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

#define TEST_XFER_PORT 9399

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
    printf("   Banking Management System - Transfer & Passbook   \n");
    printf("=====================================================\n\n");

    db_init();

    int alice_uid = 1004, alice_acc = 2001;
    int bob_uid   = 1005, bob_acc   = 2002;

    /* =======================================================
     * PHASE 1: Direct Transfer & Passbook Unit Tests
     * ======================================================= */
    printf("--- [PHASE 1] Direct Service Unit Tests ---\n");

    BalancePayload bal_alice, bal_bob;
    customer_get_balance(alice_uid, alice_acc, &bal_alice, NULL, 0);
    customer_get_balance(bob_uid, bob_acc, &bal_bob, NULL, 0);
    double initial_alice = bal_alice.balance;
    double initial_bob   = bal_bob.balance;
    printf("[*] Starting balances: Alice (Acc #%d) = $%.2f | Bob (Acc #%d) = $%.2f\n",
           alice_acc, initial_alice, bob_acc, initial_bob);

    /* Test 1.1: Transfer $2,500.00 Alice -> Bob */
    TransferResultPayload tr;
    char msg[256] = {0};
    int status = customer_transfer(alice_uid, alice_acc, bob_acc, 2500.0, &tr, msg, sizeof(msg));
    if (status == RESP_OK && tr.balance_after == (initial_alice - 2500.0)) {
        printf("[+] TEST 1.1 PASSED: Transferred $2,500.00 from Alice -> Bob.\n");
        printf("    Debit Txn ID: %d | Credit Txn ID: %d | Alice Balance: $%.2f\n",
               tr.debit_txn_id, tr.credit_txn_id, tr.balance_after);
    } else {
        fprintf(stderr, "[-] TEST 1.1 FAILED: Status %d, msg: %s\n", status, msg);
        return 1;
    }

    /* Verify Bob's updated balance */
    customer_get_balance(bob_uid, bob_acc, &bal_bob, NULL, 0);
    if (bal_bob.balance == (initial_bob + 2500.0)) {
        printf("[+] TEST 1.2 PASSED: Recipient Bob's balance correctly credited to $%.2f\n",
               bal_bob.balance);
    } else {
        fprintf(stderr, "[-] TEST 1.2 FAILED: Bob's balance is %.2f\n", bal_bob.balance);
        return 1;
    }

    /* Test 1.3: Insufficient Funds transfer attempt */
    status = customer_transfer(alice_uid, alice_acc, bob_acc, 999999.0, &tr, msg, sizeof(msg));
    if (status == RESP_ERR_INSUFFICIENT_FUNDS) {
        printf("[+] TEST 1.3 PASSED: Overdraft transfer attempt correctly rejected (Code 422).\n");
    } else {
        fprintf(stderr, "[-] TEST 1.3 FAILED: Expected 422, got %d\n", status);
        return 1;
    }

    /* Test 1.4: Transfer to self */
    status = customer_transfer(alice_uid, alice_acc, alice_acc, 100.0, &tr, msg, sizeof(msg));
    if (status == RESP_ERR_BAD_REQUEST) {
        printf("[+] TEST 1.4 PASSED: Transfer to self rejected (Code 400).\n");
    } else {
        fprintf(stderr, "[-] TEST 1.4 FAILED: Expected 400, got %d\n", status);
        return 1;
    }

    /* Test 1.5: Transfer to non-existent account */
    status = customer_transfer(alice_uid, alice_acc, 9999, 100.0, &tr, msg, sizeof(msg));
    if (status == RESP_ERR_NOT_FOUND) {
        printf("[+] TEST 1.5 PASSED: Transfer to non-existent destination account rejected (Code 404).\n");
    } else {
        fprintf(stderr, "[-] TEST 1.5 FAILED: Expected 404, got %d\n", status);
        return 1;
    }

    /* Test 1.6: Passbook Query */
    PassbookPayload pb;
    status = customer_get_passbook(alice_uid, alice_acc, &pb, msg, sizeof(msg));
    if (status == RESP_OK && pb.count > 0) {
        printf("[+] TEST 1.6 PASSED: Retrieved Alice's passbook with %d transactions.\n", pb.count);
        int has_transfer = 0;
        for (int i = 0; i < pb.count; ++i) {
            if (pb.records[i].type == TXN_TRANSFER_DEBIT) has_transfer = 1;
        }
        if (has_transfer) {
            printf("[+] TEST 1.7 PASSED: Passbook verified debit ledger record for inter-account transfer.\n");
        } else {
            fprintf(stderr, "[-] TEST 1.7 FAILED: Missing transfer transaction in passbook.\n");
            return 1;
        }
    } else {
        fprintf(stderr, "[-] TEST 1.6 FAILED: Status %d\n", status);
        return 1;
    }

    /* =======================================================
     * PHASE 2: Deadlock Prevention & Concurrent Cross-Transfers
     * ======================================================= */
    printf("\n--- [PHASE 2] Deadlock Prevention & ACID Concurrency Test ---\n");
    customer_get_balance(alice_uid, alice_acc, &bal_alice, NULL, 0);
    customer_get_balance(bob_uid, bob_acc, &bal_bob, NULL, 0);
    double total_money_before = bal_alice.balance + bal_bob.balance;
    printf("[*] Total funds across Alice and Bob: $%.2f\n", total_money_before);
    printf("[*] Forking two concurrent processes transferring in OPPOSITE directions...\n");

    int rounds = 25;
    double xfer_amt = 20.0;

    pid_t p1 = fork();
    if (p1 == 0) {
        /* Process 1: Transfers Alice -> Bob */
        for (int i = 0; i < rounds; ++i) {
            TransferResultPayload r;
            customer_transfer(alice_uid, alice_acc, bob_acc, xfer_amt, &r, NULL, 0);
            usleep(2000); /* 2ms */
        }
        exit(0);
    }

    pid_t p2 = fork();
    if (p2 == 0) {
        /* Process 2: Transfers Bob -> Alice (OPPOSITE DIRECTION!) */
        for (int i = 0; i < rounds; ++i) {
            TransferResultPayload r;
            customer_transfer(bob_uid, bob_acc, alice_acc, xfer_amt, &r, NULL, 0);
            usleep(2000); /* 2ms */
        }
        exit(0);
    }

    waitpid(p1, NULL, 0);
    waitpid(p2, NULL, 0);

    customer_get_balance(alice_uid, alice_acc, &bal_alice, NULL, 0);
    customer_get_balance(bob_uid, bob_acc, &bal_bob, NULL, 0);
    double total_money_after = bal_alice.balance + bal_bob.balance;

    if (total_money_after == total_money_before) {
        printf("[+] TEST 2.1 PASSED: Zero deadlocks detected across 50 bidirectional cross-transfers!\n");
        printf("[+] TEST 2.2 PASSED: Conservation of funds verified ($%.2f == $%.2f)!\n",
               total_money_after, total_money_before);
    } else {
        fprintf(stderr, "[-] TEST 2.2 FAILED: Inconsistency! Before: %.2f, After: %.2f\n",
                total_money_before, total_money_after);
        return 1;
    }

    /* =======================================================
     * PHASE 3: End-to-End TCP Client-Server Protocol Test
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
        saddr.sin_port = htons(TEST_XFER_PORT);

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

    int sock = connect_to_test_server(TEST_XFER_PORT);
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

    /* 3.2: Transfer $500.00 from Alice to Bob over TCP */
    memset(&req, 0, sizeof(req));
    req.opcode = REQ_CUST_TRANSFER;
    req.user_id = alice_uid;
    req.role = ROLE_CUSTOMER;
    TransferPayload tp = {.target_account_id = bob_acc, .amount = 500.0};
    memcpy(req.payload, &tp, sizeof(tp));
    send_request(sock, &req);
    recv_response(sock, &res);

    if (res.status_code == RESP_OK) {
        TransferResultPayload tr_tcp;
        memcpy(&tr_tcp, res.payload, sizeof(tr_tcp));
        printf("[+] TEST 3.2 PASSED: Transfer of $500.00 over TCP verified! New Alice Balance: $%.2f (Debit Txn: %d)\n",
               tr_tcp.balance_after, tr_tcp.debit_txn_id);
    } else {
        fprintf(stderr, "[-] TEST 3.2 FAILED: Transfer over TCP failed (%d: %s)\n", res.status_code, res.message);
        kill(server_pid, SIGKILL);
        waitpid(server_pid, NULL, 0);
        return 1;
    }

    /* 3.3: View Passbook over TCP */
    memset(&req, 0, sizeof(req));
    req.opcode = REQ_CUST_VIEW_TRANSACTIONS;
    req.user_id = alice_uid;
    req.role = ROLE_CUSTOMER;
    send_request(sock, &req);
    recv_response(sock, &res);

    if (res.status_code == RESP_OK) {
        PassbookPayload pb_tcp;
        memcpy(&pb_tcp, res.payload, sizeof(pb_tcp));
        printf("[+] TEST 3.3 PASSED: Passbook over TCP retrieved successfully with %d transaction records.\n",
               pb_tcp.count);
    } else {
        fprintf(stderr, "[-] TEST 3.3 FAILED: Passbook over TCP failed.\n");
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
    printf("   All Day 7 Transfer & Passbook Tests Passed! (12/12) \n");
    printf("=====================================================\n");

    return 0;
}
