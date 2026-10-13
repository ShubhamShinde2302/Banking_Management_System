#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include "server/db.h"

/* Initialize binary data files */
int db_init(void) {
    const char *files[] = {
        FILE_USERS,
        FILE_ACCOUNTS,
        FILE_TRANSACTIONS,
        FILE_LOANS,
        FILE_FEEDBACK
    };

    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        int fd = open(files[i], O_CREAT | O_RDWR, 0644);
        if (fd < 0) {
            return -1;
        }
        close(fd);
    }
    return 0;
}

/* ==================== USER OPERATIONS ==================== */

int db_get_next_user_id(void) {
    int fd = open(FILE_USERS, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return USER_ID_BASE;
    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    if (size < 0) return USER_ID_BASE;
    return USER_ID_BASE + (int)(size / sizeof(UserRecord));
}

int db_write_user(const UserRecord *user) {
    int fd = open(FILE_USERS, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return -1;
    ssize_t written = write(fd, user, sizeof(UserRecord));
    close(fd);
    return (written == (ssize_t)sizeof(UserRecord)) ? 0 : -1;
}

int db_read_user_by_id(int user_id, UserRecord *out) {
    int fd = open(FILE_USERS, O_RDONLY, 0644);
    if (fd < 0) return -1;

    /* First attempt: O(1) indexed lookup */
    if (user_id >= USER_ID_BASE) {
        off_t offset = (off_t)(user_id - USER_ID_BASE) * sizeof(UserRecord);
        if (lseek(fd, offset, SEEK_SET) != -1) {
            if (read(fd, out, sizeof(UserRecord)) == (ssize_t)sizeof(UserRecord)) {
                if (out->id == user_id) {
                    close(fd);
                    return 0;
                }
            }
        }
    }

    /* Fallback: Linear scan */
    lseek(fd, 0, SEEK_SET);
    UserRecord temp;
    while (read(fd, &temp, sizeof(UserRecord)) == (ssize_t)sizeof(UserRecord)) {
        if (temp.id == user_id) {
            *out = temp;
            close(fd);
            return 0;
        }
    }

    close(fd);
    return -1;
}

int db_read_user_by_username(const char *username, UserRecord *out) {
    int fd = open(FILE_USERS, O_RDONLY, 0644);
    if (fd < 0) return -1;

    UserRecord temp;
    while (read(fd, &temp, sizeof(UserRecord)) == (ssize_t)sizeof(UserRecord)) {
        if (strncmp(temp.username, username, sizeof(temp.username)) == 0) {
            *out = temp;
            close(fd);
            return 0;
        }
    }

    close(fd);
    return -1;
}

int db_update_user(const UserRecord *user) {
    int fd = open(FILE_USERS, O_RDWR, 0644);
    if (fd < 0) return -1;

    /* Attempt indexed seek first */
    if (user->id >= USER_ID_BASE) {
        off_t offset = (off_t)(user->id - USER_ID_BASE) * sizeof(UserRecord);
        if (lseek(fd, offset, SEEK_SET) != -1) {
            UserRecord check;
            if (read(fd, &check, sizeof(UserRecord)) == (ssize_t)sizeof(UserRecord)) {
                if (check.id == user->id) {
                    lseek(fd, offset, SEEK_SET);
                    ssize_t written = write(fd, user, sizeof(UserRecord));
                    close(fd);
                    return (written == (ssize_t)sizeof(UserRecord)) ? 0 : -1;
                }
            }
        }
    }

    /* Fallback scan */
    lseek(fd, 0, SEEK_SET);
    UserRecord temp;
    off_t current_pos = 0;
    while (read(fd, &temp, sizeof(UserRecord)) == (ssize_t)sizeof(UserRecord)) {
        if (temp.id == user->id) {
            lseek(fd, current_pos, SEEK_SET);
            ssize_t written = write(fd, user, sizeof(UserRecord));
            close(fd);
            return (written == (ssize_t)sizeof(UserRecord)) ? 0 : -1;
        }
        current_pos += sizeof(UserRecord);
    }

    close(fd);
    return -1;
}

int db_get_all_users(UserRecord *out_list, int max_count) {
    int fd = open(FILE_USERS, O_RDONLY, 0644);
    if (fd < 0) return 0;

    int count = 0;
    while (count < max_count && read(fd, &out_list[count], sizeof(UserRecord)) == (ssize_t)sizeof(UserRecord)) {
        count++;
    }
    close(fd);
    return count;
}

/* ==================== ACCOUNT OPERATIONS ==================== */

int db_get_next_account_id(void) {
    int fd = open(FILE_ACCOUNTS, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return ACCOUNT_ID_BASE;
    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    if (size < 0) return ACCOUNT_ID_BASE;
    return ACCOUNT_ID_BASE + (int)(size / sizeof(AccountRecord));
}

int db_write_account(const AccountRecord *acc) {
    int fd = open(FILE_ACCOUNTS, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return -1;
    ssize_t written = write(fd, acc, sizeof(AccountRecord));
    close(fd);
    return (written == (ssize_t)sizeof(AccountRecord)) ? 0 : -1;
}

int db_read_account_by_id(int account_id, AccountRecord *out) {
    int fd = open(FILE_ACCOUNTS, O_RDONLY, 0644);
    if (fd < 0) return -1;

    if (account_id >= ACCOUNT_ID_BASE) {
        off_t offset = (off_t)(account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);
        if (lseek(fd, offset, SEEK_SET) != -1) {
            if (read(fd, out, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
                if (out->account_id == account_id) {
                    close(fd);
                    return 0;
                }
            }
        }
    }

    lseek(fd, 0, SEEK_SET);
    AccountRecord temp;
    while (read(fd, &temp, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
        if (temp.account_id == account_id) {
            *out = temp;
            close(fd);
            return 0;
        }
    }

    close(fd);
    return -1;
}

int db_read_account_by_user_id(int user_id, AccountRecord *out) {
    int fd = open(FILE_ACCOUNTS, O_RDONLY, 0644);
    if (fd < 0) return -1;

    AccountRecord temp;
    while (read(fd, &temp, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
        if (temp.user_id == user_id) {
            *out = temp;
            close(fd);
            return 0;
        }
    }

    close(fd);
    return -1;
}

int db_update_account(const AccountRecord *acc) {
    int fd = open(FILE_ACCOUNTS, O_RDWR, 0644);
    if (fd < 0) return -1;

    if (acc->account_id >= ACCOUNT_ID_BASE) {
        off_t offset = (off_t)(acc->account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);
        if (lseek(fd, offset, SEEK_SET) != -1) {
            AccountRecord check;
            if (read(fd, &check, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
                if (check.account_id == acc->account_id) {
                    lseek(fd, offset, SEEK_SET);
                    ssize_t written = write(fd, acc, sizeof(AccountRecord));
                    close(fd);
                    return (written == (ssize_t)sizeof(AccountRecord)) ? 0 : -1;
                }
            }
        }
    }

    lseek(fd, 0, SEEK_SET);
    AccountRecord temp;
    off_t current_pos = 0;
    while (read(fd, &temp, sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
        if (temp.account_id == acc->account_id) {
            lseek(fd, current_pos, SEEK_SET);
            ssize_t written = write(fd, acc, sizeof(AccountRecord));
            close(fd);
            return (written == (ssize_t)sizeof(AccountRecord)) ? 0 : -1;
        }
        current_pos += sizeof(AccountRecord);
    }

    close(fd);
    return -1;
}

int db_get_all_accounts(AccountRecord *out_list, int max_count) {
    int fd = open(FILE_ACCOUNTS, O_RDONLY, 0644);
    if (fd < 0) return 0;

    int count = 0;
    while (count < max_count && read(fd, &out_list[count], sizeof(AccountRecord)) == (ssize_t)sizeof(AccountRecord)) {
        count++;
    }
    close(fd);
    return count;
}

/* ==================== TRANSACTION OPERATIONS ==================== */

int db_get_next_txn_id(void) {
    int fd = open(FILE_TRANSACTIONS, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return TXN_ID_BASE;
    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    if (size < 0) return TXN_ID_BASE;
    return TXN_ID_BASE + (int)(size / sizeof(TransactionRecord));
}

int db_write_transaction(const TransactionRecord *txn) {
    int fd = open(FILE_TRANSACTIONS, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return -1;
    ssize_t written = write(fd, txn, sizeof(TransactionRecord));
    close(fd);
    return (written == (ssize_t)sizeof(TransactionRecord)) ? 0 : -1;
}

int db_get_transactions_by_account(int account_id, TransactionRecord *out_list, int max_count) {
    int fd = open(FILE_TRANSACTIONS, O_RDONLY, 0644);
    if (fd < 0) return 0;

    int count = 0;
    TransactionRecord temp;
    while (count < max_count && read(fd, &temp, sizeof(TransactionRecord)) == (ssize_t)sizeof(TransactionRecord)) {
        if (temp.account_id == account_id || temp.target_account_id == account_id) {
            out_list[count++] = temp;
        }
    }
    close(fd);
    return count;
}

/* ==================== LOAN OPERATIONS ==================== */

int db_get_next_loan_id(void) {
    int fd = open(FILE_LOANS, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return LOAN_ID_BASE;
    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    if (size < 0) return LOAN_ID_BASE;
    return LOAN_ID_BASE + (int)(size / sizeof(LoanRecord));
}

int db_write_loan(const LoanRecord *loan) {
    int fd = open(FILE_LOANS, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return -1;
    ssize_t written = write(fd, loan, sizeof(LoanRecord));
    close(fd);
    return (written == (ssize_t)sizeof(LoanRecord)) ? 0 : -1;
}

int db_read_loan_by_id(int loan_id, LoanRecord *out) {
    int fd = open(FILE_LOANS, O_RDONLY, 0644);
    if (fd < 0) return -1;

    if (loan_id >= LOAN_ID_BASE) {
        off_t offset = (off_t)(loan_id - LOAN_ID_BASE) * sizeof(LoanRecord);
        if (lseek(fd, offset, SEEK_SET) != -1) {
            if (read(fd, out, sizeof(LoanRecord)) == (ssize_t)sizeof(LoanRecord)) {
                if (out->loan_id == loan_id) {
                    close(fd);
                    return 0;
                }
            }
        }
    }

    lseek(fd, 0, SEEK_SET);
    LoanRecord temp;
    while (read(fd, &temp, sizeof(LoanRecord)) == (ssize_t)sizeof(LoanRecord)) {
        if (temp.loan_id == loan_id) {
            *out = temp;
            close(fd);
            return 0;
        }
    }

    close(fd);
    return -1;
}

int db_update_loan(const LoanRecord *loan) {
    int fd = open(FILE_LOANS, O_RDWR, 0644);
    if (fd < 0) return -1;

    if (loan->loan_id >= LOAN_ID_BASE) {
        off_t offset = (off_t)(loan->loan_id - LOAN_ID_BASE) * sizeof(LoanRecord);
        if (lseek(fd, offset, SEEK_SET) != -1) {
            LoanRecord check;
            if (read(fd, &check, sizeof(LoanRecord)) == (ssize_t)sizeof(LoanRecord)) {
                if (check.loan_id == loan->loan_id) {
                    lseek(fd, offset, SEEK_SET);
                    ssize_t written = write(fd, loan, sizeof(LoanRecord));
                    close(fd);
                    return (written == (ssize_t)sizeof(LoanRecord)) ? 0 : -1;
                }
            }
        }
    }

    lseek(fd, 0, SEEK_SET);
    LoanRecord temp;
    off_t current_pos = 0;
    while (read(fd, &temp, sizeof(LoanRecord)) == (ssize_t)sizeof(LoanRecord)) {
        if (temp.loan_id == loan->loan_id) {
            lseek(fd, current_pos, SEEK_SET);
            ssize_t written = write(fd, loan, sizeof(LoanRecord));
            close(fd);
            return (written == (ssize_t)sizeof(LoanRecord)) ? 0 : -1;
        }
        current_pos += sizeof(LoanRecord);
    }

    close(fd);
    return -1;
}

int db_get_all_loans(LoanRecord *out_list, int max_count) {
    int fd = open(FILE_LOANS, O_RDONLY, 0644);
    if (fd < 0) return 0;

    int count = 0;
    while (count < max_count && read(fd, &out_list[count], sizeof(LoanRecord)) == (ssize_t)sizeof(LoanRecord)) {
        count++;
    }
    close(fd);
    return count;
}

int db_get_loans_by_employee(int employee_id, LoanRecord *out_list, int max_count) {
    int fd = open(FILE_LOANS, O_RDONLY, 0644);
    if (fd < 0) return 0;

    int count = 0;
    LoanRecord temp;
    while (count < max_count && read(fd, &temp, sizeof(LoanRecord)) == (ssize_t)sizeof(LoanRecord)) {
        if (temp.assigned_employee_id == employee_id) {
            out_list[count++] = temp;
        }
    }
    close(fd);
    return count;
}

/* ==================== FEEDBACK OPERATIONS ==================== */

int db_get_next_feedback_id(void) {
    int fd = open(FILE_FEEDBACK, O_RDONLY | O_CREAT, 0644);
    if (fd < 0) return FEEDBACK_ID_BASE;
    off_t size = lseek(fd, 0, SEEK_END);
    close(fd);
    if (size < 0) return FEEDBACK_ID_BASE;
    return FEEDBACK_ID_BASE + (int)(size / sizeof(FeedbackRecord));
}

int db_write_feedback(const FeedbackRecord *fb) {
    int fd = open(FILE_FEEDBACK, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return -1;
    ssize_t written = write(fd, fb, sizeof(FeedbackRecord));
    close(fd);
    return (written == (ssize_t)sizeof(FeedbackRecord)) ? 0 : -1;
}

int db_get_all_feedbacks(FeedbackRecord *out_list, int max_count) {
    int fd = open(FILE_FEEDBACK, O_RDONLY, 0644);
    if (fd < 0) return 0;

    int count = 0;
    while (count < max_count && read(fd, &out_list[count], sizeof(FeedbackRecord)) == (ssize_t)sizeof(FeedbackRecord)) {
        count++;
    }
    close(fd);
    return count;
}
