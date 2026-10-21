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

int customer_transfer(int user_id,
                      int source_account_id,
                      int target_account_id,
                      double amount,
                      TransferResultPayload *out_res,
                      char *msg_out,
                      size_t msg_len) {
    if (amount <= 0.0) {
        if (msg_out) snprintf(msg_out, msg_len, "Transfer amount must be strictly positive.");
        return RESP_ERR_BAD_REQUEST;
    }

    if (source_account_id < ACCOUNT_ID_BASE || target_account_id < ACCOUNT_ID_BASE) {
        if (msg_out) snprintf(msg_out, msg_len, "Invalid account number specified.");
        return RESP_ERR_BAD_REQUEST;
    }

    if (source_account_id == target_account_id) {
        if (msg_out) snprintf(msg_out, msg_len, "Cannot transfer funds to the same account.");
        return RESP_ERR_BAD_REQUEST;
    }

    int fd = open(FILE_ACCOUNTS, O_RDWR, 0644);
    if (fd < 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Failed to open accounts storage.");
        return RESP_ERR_INTERNAL;
    }

    /* Deadlock-free two-phase locking via resource ordering */
    if (lock_transfer_accounts(fd, source_account_id, target_account_id) < 0) {
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Transfer lock could not be acquired (accounts busy).");
        return RESP_ERR_RECORD_LOCKED;
    }

    off_t src_offset = (off_t)(source_account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);
    off_t dst_offset = (off_t)(target_account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);

    AccountRecord src_acc, dst_acc;
    memset(&src_acc, 0, sizeof(src_acc));
    memset(&dst_acc, 0, sizeof(dst_acc));

    int src_found = 0, dst_found = 0;

    if (lseek(fd, src_offset, SEEK_SET) != -1 &&
        read(fd, &src_acc, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
        if (src_acc.account_id == source_account_id) {
            src_found = 1;
        }
    }

    if (lseek(fd, dst_offset, SEEK_SET) != -1 &&
        read(fd, &dst_acc, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
        if (dst_acc.account_id == target_account_id) {
            dst_found = 1;
        }
    }

    if (!src_found) {
        unlock_transfer_accounts(fd, source_account_id, target_account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Source account #%d not found.", source_account_id);
        return RESP_ERR_NOT_FOUND;
    }

    if (!dst_found) {
        unlock_transfer_accounts(fd, source_account_id, target_account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Destination account #%d not found.", target_account_id);
        return RESP_ERR_NOT_FOUND;
    }

    /* Verify authorization */
    if (user_id > 0 && src_acc.user_id != user_id) {
        unlock_transfer_accounts(fd, source_account_id, target_account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Unauthorized: Source account does not belong to you.");
        return RESP_ERR_UNAUTHORIZED;
    }

    /* Verify account statuses */
    if (src_acc.is_active != STATUS_ACTIVE) {
        unlock_transfer_accounts(fd, source_account_id, target_account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Source account #%d is deactivated or frozen.", source_account_id);
        return RESP_ERR_ACCOUNT_INACTIVE;
    }

    if (dst_acc.is_active != STATUS_ACTIVE) {
        unlock_transfer_accounts(fd, source_account_id, target_account_id);
        close(fd);
        if (msg_out) snprintf(msg_out, msg_len, "Destination account #%d is deactivated or frozen.", target_account_id);
        return RESP_ERR_ACCOUNT_INACTIVE;
    }

    /* Verify sufficient funds */
    if (src_acc.balance < amount) {
        unlock_transfer_accounts(fd, source_account_id, target_account_id);
        close(fd);
        if (msg_out) {
            snprintf(msg_out, msg_len,
                     "Insufficient funds: Transfer of $%.2f exceeds available balance $%.2f.",
                     amount, src_acc.balance);
        }
        return RESP_ERR_INSUFFICIENT_FUNDS;
    }

    /* Execute atomic transfer */
    src_acc.balance -= amount;
    dst_acc.balance += amount;

    lseek(fd, src_offset, SEEK_SET);
    write(fd, &src_acc, sizeof(AccountRecord));

    lseek(fd, dst_offset, SEEK_SET);
    write(fd, &dst_acc, sizeof(AccountRecord));

    fsync(fd); /* ACID durability */

    /* Release transfer locks */
    unlock_transfer_accounts(fd, source_account_id, target_account_id);
    close(fd);

    time_t now = time(NULL);

    /* Ledger 1: Debit from sender */
    TransactionRecord txn_debit;
    memset(&txn_debit, 0, sizeof(txn_debit));
    txn_debit.transaction_id = db_get_next_txn_id();
    txn_debit.account_id = source_account_id;
    txn_debit.target_account_id = target_account_id;
    txn_debit.type = TXN_TRANSFER_DEBIT;
    txn_debit.amount = amount;
    txn_debit.balance_after = src_acc.balance;
    txn_debit.timestamp = now;
    snprintf(txn_debit.remarks, sizeof(txn_debit.remarks),
             "Transfer to Acc #%d (%s)", target_account_id, dst_acc.customer_name);
    db_write_transaction(&txn_debit);

    /* Ledger 2: Credit to recipient */
    TransactionRecord txn_credit;
    memset(&txn_credit, 0, sizeof(txn_credit));
    txn_credit.transaction_id = db_get_next_txn_id();
    txn_credit.account_id = target_account_id;
    txn_credit.target_account_id = source_account_id;
    txn_credit.type = TXN_TRANSFER_CREDIT;
    txn_credit.amount = amount;
    txn_credit.balance_after = dst_acc.balance;
    txn_credit.timestamp = now;
    snprintf(txn_credit.remarks, sizeof(txn_credit.remarks),
             "Transfer from Acc #%d (%s)", source_account_id, src_acc.customer_name);
    db_write_transaction(&txn_credit);

    if (out_res) {
        out_res->debit_txn_id = txn_debit.transaction_id;
        out_res->credit_txn_id = txn_credit.transaction_id;
        out_res->source_account_id = source_account_id;
        out_res->target_account_id = target_account_id;
        out_res->amount = amount;
        out_res->balance_after = src_acc.balance;
        out_res->timestamp = now;
    }

    if (msg_out) {
        snprintf(msg_out, msg_len,
                 "Transferred $%.2f to Acc #%d (%s). Remaining Balance: $%.2f (Debit Txn: %d)",
                 amount, target_account_id, dst_acc.customer_name, src_acc.balance, txn_debit.transaction_id);
    }

    return RESP_OK;
}

int customer_get_passbook(int user_id,
                          int account_id,
                          PassbookPayload *out_passbook,
                          char *msg_out,
                          size_t msg_len) {
    if (account_id < ACCOUNT_ID_BASE || !out_passbook) {
        if (msg_out) snprintf(msg_out, msg_len, "Invalid account number.");
        return RESP_ERR_BAD_REQUEST;
    }

    AccountRecord acc;
    if (db_read_account_by_id(account_id, &acc) != 0) {
        if (msg_out) snprintf(msg_out, msg_len, "Account #%d does not exist.", account_id);
        return RESP_ERR_NOT_FOUND;
    }

    if (user_id > 0 && acc.user_id != user_id) {
        if (msg_out) snprintf(msg_out, msg_len, "Unauthorized access to passbook.");
        return RESP_ERR_UNAUTHORIZED;
    }

    TransactionRecord all_txns[256];
    int total = db_get_transactions_by_account(account_id, all_txns, 256);

    memset(out_passbook, 0, sizeof(PassbookPayload));
    if (total <= MAX_PASSBOOK_ENTRIES) {
        out_passbook->count = total;
        for (int i = 0; i < total; ++i) {
            out_passbook->records[i] = all_txns[i];
        }
    } else {
        out_passbook->count = MAX_PASSBOOK_ENTRIES;
        int start = total - MAX_PASSBOOK_ENTRIES;
        for (int i = 0; i < MAX_PASSBOOK_ENTRIES; ++i) {
            out_passbook->records[i] = all_txns[start + i];
        }
    }

    if (msg_out) {
        snprintf(msg_out, msg_len, "Passbook retrieved (%d recent transaction%s).",
                 out_passbook->count, out_passbook->count == 1 ? "" : "s");
    }

    return RESP_OK;
}
