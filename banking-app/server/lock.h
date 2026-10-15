#ifndef LOCK_H
#define LOCK_H

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include "common/models.h"
#include "server/db.h"

/* Record lock types */
typedef enum {
    LOCK_READ,
    LOCK_WRITE,
    LOCK_UNLOCK
} RecordLockType;

/* Low-level fcntl record lock helper */
int set_record_lock(int fd, int lock_type, off_t offset, off_t length, int blocking);

/* Account record locking (for balance query, deposit, withdraw) */
int lock_account(int fd, int account_id, RecordLockType type, int blocking);
int unlock_account(int fd, int account_id);

/* Deadlock-free two-account write locking (for funds transfer) */
int lock_transfer_accounts(int fd, int acc_id_1, int acc_id_2);
int unlock_transfer_accounts(int fd, int acc_id_1, int acc_id_2);

/* User record locking (for profile / role updates) */
int lock_user_record(int fd, int user_id, RecordLockType type, int blocking);
int unlock_user_record(int fd, int user_id);

/* Single-session enforcement (one active session per user) */
int acquire_user_session(int user_id, int *session_fd_out);
int release_user_session(int user_id, int session_fd);

/* Loan record locking */
int lock_loan_record(int fd, int loan_id, RecordLockType type, int blocking);
int unlock_loan_record(int fd, int loan_id);

#endif /* LOCK_H */
