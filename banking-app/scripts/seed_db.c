#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "common/models.h"
#include "server/db.h"

int main(void) {
    printf("===================================================\n");
    printf("   Banking Management System - Database Seeder     \n");
    printf("===================================================\n\n");

    /* Truncate / reset existing files to ensure clean state */
    int fds[] = {
        open(FILE_USERS, O_WRONLY | O_CREAT | O_TRUNC, 0644),
        open(FILE_ACCOUNTS, O_WRONLY | O_CREAT | O_TRUNC, 0644),
        open(FILE_TRANSACTIONS, O_WRONLY | O_CREAT | O_TRUNC, 0644),
        open(FILE_LOANS, O_WRONLY | O_CREAT | O_TRUNC, 0644),
        open(FILE_FEEDBACK, O_WRONLY | O_CREAT | O_TRUNC, 0644)
    };

    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); ++i) {
        if (fds[i] >= 0) {
            close(fds[i]);
        }
    }

    if (db_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize data files.\n");
        return 1;
    }

    time_t now = time(NULL);

    /* 1. Seed Administrator */
    UserRecord admin = {
        .id = 1001,
        .username = "admin",
        .password = "adminpassword",
        .role = ROLE_ADMIN,
        .account_id = 0,
        .is_active = STATUS_ACTIVE,
        .is_logged_in = SESSION_LOGGED_OUT,
        .session_pid = 0
    };
    db_write_user(&admin);
    printf("[+] Seeded Admin:      username='%s', id=%d\n", admin.username, admin.id);

    /* 2. Seed Manager */
    UserRecord manager = {
        .id = 1002,
        .username = "manager1",
        .password = "managerpassword",
        .role = ROLE_MANAGER,
        .account_id = 0,
        .is_active = STATUS_ACTIVE,
        .is_logged_in = SESSION_LOGGED_OUT,
        .session_pid = 0
    };
    db_write_user(&manager);
    printf("[+] Seeded Manager:    username='%s', id=%d\n", manager.username, manager.id);

    /* 3. Seed Bank Employee */
    UserRecord employee = {
        .id = 1003,
        .username = "employee1",
        .password = "employeepassword",
        .role = ROLE_EMPLOYEE,
        .account_id = 0,
        .is_active = STATUS_ACTIVE,
        .is_logged_in = SESSION_LOGGED_OUT,
        .session_pid = 0
    };
    db_write_user(&employee);
    printf("[+] Seeded Employee:   username='%s', id=%d\n", employee.username, employee.id);

    /* 4. Seed Customer 1 (Alice) */
    UserRecord alice = {
        .id = 1004,
        .username = "alice",
        .password = "alicepassword",
        .role = ROLE_CUSTOMER,
        .account_id = 2001,
        .is_active = STATUS_ACTIVE,
        .is_logged_in = SESSION_LOGGED_OUT,
        .session_pid = 0
    };
    db_write_user(&alice);

    AccountRecord alice_acc = {
        .account_id = 2001,
        .user_id = 1004,
        .customer_name = "Alice Smith",
        .balance = 15000.0,
        .is_active = STATUS_ACTIVE,
        .created_at = now
    };
    db_write_account(&alice_acc);
    printf("[+] Seeded Customer 1: username='%s', id=%d, account=%d, balance=%.2f\n",
           alice.username, alice.id, alice_acc.account_id, alice_acc.balance);

    /* 5. Seed Customer 2 (Bob) */
    UserRecord bob = {
        .id = 1005,
        .username = "bob",
        .password = "bobpassword",
        .role = ROLE_CUSTOMER,
        .account_id = 2002,
        .is_active = STATUS_ACTIVE,
        .is_logged_in = SESSION_LOGGED_OUT,
        .session_pid = 0
    };
    db_write_user(&bob);

    AccountRecord bob_acc = {
        .account_id = 2002,
        .user_id = 1005,
        .customer_name = "Bob Jones",
        .balance = 8000.0,
        .is_active = STATUS_ACTIVE,
        .created_at = now
    };
    db_write_account(&bob_acc);
    printf("[+] Seeded Customer 2: username='%s', id=%d, account=%d, balance=%.2f\n",
           bob.username, bob.id, bob_acc.account_id, bob_acc.balance);

    /* 6. Seed Sample Transactions */
    TransactionRecord txn1 = {
        .transaction_id = 5001,
        .account_id = 2001,
        .target_account_id = 0,
        .type = TXN_DEPOSIT,
        .amount = 15000.0,
        .balance_after = 15000.0,
        .timestamp = now,
        .remarks = "Initial account opening deposit"
    };
    db_write_transaction(&txn1);

    TransactionRecord txn2 = {
        .transaction_id = 5002,
        .account_id = 2002,
        .target_account_id = 0,
        .type = TXN_DEPOSIT,
        .amount = 8000.0,
        .balance_after = 8000.0,
        .timestamp = now,
        .remarks = "Initial account opening deposit"
    };
    db_write_transaction(&txn2);
    printf("[+] Seeded Initial Opening Transactions for Alice and Bob.\n");

    /* 7. Seed Sample Loan Application */
    LoanRecord loan = {
        .loan_id = 8001,
        .customer_id = 1004,
        .account_id = 2001,
        .amount = 50000.0,
        .assigned_employee_id = 0, /* unassigned, for Manager to assign */
        .status = LOAN_PENDING,
        .reason = "Home renovation loan",
        .remarks = "Pending assignment by bank manager",
        .applied_at = now,
        .processed_at = 0
    };
    db_write_loan(&loan);
    printf("[+] Seeded Sample Loan: id=%d, customer=%d, amount=%.2f (Pending Assignment)\n",
           loan.loan_id, loan.customer_id, loan.amount);

    /* 8. Seed Sample Feedback */
    FeedbackRecord fb = {
        .feedback_id = 9001,
        .customer_id = 1004,
        .customer_name = "Alice Smith",
        .message = "The online banking service is smooth and convenient!",
        .submitted_at = now
    };
    db_write_feedback(&fb);
    printf("[+] Seeded Sample Feedback from Alice Smith.\n\n");

    printf("===================================================\n");
    printf("   Database Seeding Complete Successfully!         \n");
    printf("===================================================\n");

    return 0;
}
