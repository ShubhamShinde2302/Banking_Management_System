#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include "server/customer_ops.h"
#include "server/db.h"
#include "server/lock.h"

int customer_get_balance(int user_id,
                         int account_id,
                         BalancePayload *out_bal,
                         char *msg_out,
                         size_t msg_len) {
    if (account_id < ACCOUNT_ID_BASE || !out_bal) {
        if (msg_out) snprintf(msg_out, msg_len, "Invalid account ID.");
        return RESP_ERR_BAD_REQUEST;
    }

    int fd = open(FILE_ACCOUNTS, O_RDONLY, 0644);
    if (fd < 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Failed to open accounts storage.");
        return RESP_ERR_INTERNAL;
    }

    /* Acquire shared read lock on this account's record */
    if (lock_account(fd, account_id, LOCK_READ, 1) < 0) {
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Account record is currently busy.");
        return RESP_ERR_RECORD_LOCKED;
    }

    off_t offset = (off_t)(account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);
    AccountRecord acc;
    memset(&acc, 0, sizeof(acc));

    int found = 0;
    if (lseek(fd, offset, SEEK_SET) != -1) {
        if (read(fd, &acc, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
            if (acc.account_id == account_id) {
                found = 1;
            }
        }
    }

    unlock_account(fd, account_id);
    close(fd);

    if (!found) {
        if (msg_out) snprintf(msg_out, msg_len, "Account #%d does not exist.", account_id);
        return RESP_ERR_NOT_FOUND;
    }

    /* Verify authorization */
    if (user_id > 0 && acc.user_id != user_id) {
        if (msg_out) snprintf(msg_out, msg_len, "Unauthorized access to account #%d.", account_id);
        return RESP_ERR_UNAUTHORIZED;
    }

    out_bal->account_id = acc.account_id;
    out_bal->balance = acc.balance;
    out_bal->is_active = acc.is_active;
    strncpy(out_bal->customer_name, acc.customer_name, sizeof(out_bal->customer_name) - 1);

    if (msg_out) {
        snprintf(msg_out, msg_len, "Account #%d: Available Balance = $%.2f",
                 acc.account_id, acc.balance);
    }

    return RESP_OK;
}

int customer_deposit(int user_id,
                     int account_id,
                     double amount,
                     TransactionResultPayload *out_res,
                     char *msg_out,
                     size_t msg_len) {
    if (amount <= 0.0) {
        if (msg_out) snprintf(msg_out, msg_len, "Deposit amount must be strictly positive.");
        return RESP_ERR_BAD_REQUEST;
    }

    if (account_id < ACCOUNT_ID_BASE) {
        if (msg_out) snprintf(msg_out, msg_len, "Invalid account ID #%d.", account_id);
        return RESP_ERR_BAD_REQUEST;
    }

    int fd = open(FILE_ACCOUNTS, O_RDWR, 0644);
    if (fd < 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Failed to open accounts storage.");
        return RESP_ERR_INTERNAL;
    }

    /* Acquire exclusive write lock on account record */
    if (lock_account(fd, account_id, LOCK_WRITE, 1) < 0) {
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Account record is currently locked.");
        return RESP_ERR_RECORD_LOCKED;
    }

    off_t offset = (off_t)(account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);
    AccountRecord acc;
    memset(&acc, 0, sizeof(acc));

    int found = 0;
    if (lseek(fd, offset, SEEK_SET) != -1) {
        if (read(fd, &acc, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
            if (acc.account_id == account_id) {
                found = 1;
            }
        }
    }

    if (!found) {
        unlock_account(fd, account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Account #%d does not exist.", account_id);
        return RESP_ERR_NOT_FOUND;
    }

    /* Verify authorization */
    if (user_id > 0 && acc.user_id != user_id) {
        unlock_account(fd, account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Unauthorized access to account #%d.", account_id);
        return RESP_ERR_UNAUTHORIZED;
    }

    /* Check account status */
    if (acc.is_active != STATUS_ACTIVE) {
        unlock_account(fd, account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Account #%d is deactivated or frozen.", account_id);
        return RESP_ERR_ACCOUNT_INACTIVE;
    }

    /* Execute deposit */
    acc.balance += amount;

    lseek(fd, offset, SEEK_SET);
    write(fd, &acc, sizeof(AccountRecord));
    fsync(fd); /* ACID durability */

    unlock_account(fd, account_id);
    close(fd);

    /* Record transaction */
    TransactionRecord txn;
    memset(&txn, 0, sizeof(txn));
    txn.transaction_id = db_get_next_txn_id();
    txn.account_id = acc.account_id;
    txn.target_account_id = 0;
    txn.type = TXN_DEPOSIT;
    txn.amount = amount;
    txn.balance_after = acc.balance;
    txn.timestamp = time(NULL);
    snprintf(txn.remarks, sizeof(txn.remarks), "Customer deposit of $%.2f", amount);
    db_write_transaction(&txn);

    if (out_res) {
        out_res->transaction_id = txn.transaction_id;
        out_res->account_id = acc.account_id;
        out_res->amount = amount;
        out_res->balance_after = acc.balance;
        out_res->timestamp = txn.timestamp;
    }

    if (msg_out) {
        snprintf(msg_out, msg_len,
                 "Deposit of $%.2f successful! New Balance: $%.2f (Txn ID: %d)",
                 amount, acc.balance, txn.transaction_id);
    }

    return RESP_OK;
}

int customer_withdraw(int user_id,
                      int account_id,
                      double amount,
                      TransactionResultPayload *out_res,
                      char *msg_out,
                      size_t msg_len) {
    if (amount <= 0.0) {
        if (msg_out) snprintf(msg_out, msg_len, "Withdrawal amount must be strictly positive.");
        return RESP_ERR_BAD_REQUEST;
    }

    if (account_id < ACCOUNT_ID_BASE) {
        if (msg_out) snprintf(msg_out, msg_len, "Invalid account ID #%d.", account_id);
        return RESP_ERR_BAD_REQUEST;
    }

    int fd = open(FILE_ACCOUNTS, O_RDWR, 0644);
    if (fd < 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Failed to open accounts storage.");
        return RESP_ERR_INTERNAL;
    }

    /* Acquire exclusive write lock on account record */
    if (lock_account(fd, account_id, LOCK_WRITE, 1) < 0) {
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Account record is currently locked.");
        return RESP_ERR_RECORD_LOCKED;
    }

    off_t offset = (off_t)(account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);
    AccountRecord acc;
    memset(&acc, 0, sizeof(acc));

    int found = 0;
    if (lseek(fd, offset, SEEK_SET) != -1) {
        if (read(fd, &acc, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
            if (acc.account_id == account_id) {
                found = 1;
            }
        }
    }

    if (!found) {
        unlock_account(fd, account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Account #%d does not exist.", account_id);
        return RESP_ERR_NOT_FOUND;
    }

    /* Verify authorization */
    if (user_id > 0 && acc.user_id != user_id) {
        unlock_account(fd, account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Unauthorized access to account #%d.", account_id);
        return RESP_ERR_UNAUTHORIZED;
    }

    /* Check account status */
    if (acc.is_active != STATUS_ACTIVE) {
        unlock_account(fd, account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Account #%d is deactivated or frozen.", account_id);
        return RESP_ERR_ACCOUNT_INACTIVE;
    }

    /* Check sufficient funds */
    if (acc.balance < amount) {
        unlock_account(fd, account_id);
        close(fd);
        if (msg_out) {
            snprintf(msg_out, msg_len,
                     "Insufficient funds: Requested $%.2f, but available balance is $%.2f.",
                     amount, acc.balance);
        }
        return RESP_ERR_INSUFFICIENT_FUNDS;
    }

    /* Execute withdrawal */
    acc.balance -= amount;

    lseek(fd, offset, SEEK_SET);
    write(fd, &acc, sizeof(AccountRecord));
    fsync(fd); /* ACID durability */

    unlock_account(fd, account_id);
    close(fd);

    /* Record transaction */
    TransactionRecord txn;
    memset(&txn, 0, sizeof(txn));
    txn.transaction_id = db_get_next_txn_id();
    txn.account_id = acc.account_id;
    txn.target_account_id = 0;
    txn.type = TXN_WITHDRAW;
    txn.amount = amount;
    txn.balance_after = acc.balance;
    txn.timestamp = time(NULL);
    snprintf(txn.remarks, sizeof(txn.remarks), "Customer withdrawal of $%.2f", amount);
    db_write_transaction(&txn);

    if (out_res) {
        out_res->transaction_id = txn.transaction_id;
        out_res->account_id = acc.account_id;
        out_res->amount = amount;
        out_res->balance_after = acc.balance;
        out_res->timestamp = txn.timestamp;
    }

    if (msg_out) {
        snprintf(msg_out, msg_len,
                 "Withdrawal of $%.2f successful! Remaining Balance: $%.2f (Txn ID: %d)",
                 amount, acc.balance, txn.transaction_id);
    }

    return RESP_OK;
}
