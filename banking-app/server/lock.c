#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "server/lock.h"

#ifndef F_OFD_SETLK
#define F_OFD_SETLK 37
#endif
#ifndef F_OFD_SETLKW
#define F_OFD_SETLKW 38
#endif

/* Low-level fcntl record lock helper */
int set_record_lock(int fd, int lock_type, off_t offset, off_t length, int blocking) {
    if (fd < 0 || offset < 0) return -1;

    struct flock fl;
    memset(&fl, 0, sizeof(fl));

    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = length;

    switch (lock_type) {
        case LOCK_READ:
            fl.l_type = F_RDLCK;
            break;
        case LOCK_WRITE:
            fl.l_type = F_WRLCK;
            break;
        case LOCK_UNLOCK:
            fl.l_type = F_UNLCK;
            break;
        default:
            return -1;
    }

#if defined(F_OFD_SETLK)
    fl.l_pid = 0;
    int cmd = blocking ? F_OFD_SETLKW : F_OFD_SETLK;
#else
    fl.l_pid = getpid();
    int cmd = blocking ? F_SETLKW : F_SETLK;
#endif
    return fcntl(fd, cmd, &fl);
}

/* ==================== ACCOUNT LOCKING ==================== */

int lock_account(int fd, int account_id, RecordLockType type, int blocking) {
    if (account_id < ACCOUNT_ID_BASE) return -1;
    off_t offset = (off_t)(account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);
    return set_record_lock(fd, type, offset, sizeof(AccountRecord), blocking);
}

int unlock_account(int fd, int account_id) {
    if (account_id < ACCOUNT_ID_BASE) return -1;
    off_t offset = (off_t)(account_id - ACCOUNT_ID_BASE) * sizeof(AccountRecord);
    return set_record_lock(fd, LOCK_UNLOCK, offset, sizeof(AccountRecord), 0);
}

/* Deadlock-free two-phase locking for funds transfer (resource ordering) */
int lock_transfer_accounts(int fd, int acc_id_1, int acc_id_2) {
    if (acc_id_1 <= 0 || acc_id_2 <= 0 || acc_id_1 == acc_id_2) {
        return -1;
    }

    /* Lock smaller ID first to prevent cyclic wait / deadlock */
    int first_acc  = (acc_id_1 < acc_id_2) ? acc_id_1 : acc_id_2;
    int second_acc = (acc_id_1 < acc_id_2) ? acc_id_2 : acc_id_1;

    if (lock_account(fd, first_acc, LOCK_WRITE, 1) < 0) {
        return -1;
    }

    if (lock_account(fd, second_acc, LOCK_WRITE, 1) < 0) {
        unlock_account(fd, first_acc);
        return -1;
    }

    return 0;
}

int unlock_transfer_accounts(int fd, int acc_id_1, int acc_id_2) {
    unlock_account(fd, acc_id_1);
    unlock_account(fd, acc_id_2);
    return 0;
}

/* ==================== USER LOCKING & SINGLE SESSION ==================== */

int lock_user_record(int fd, int user_id, RecordLockType type, int blocking) {
    if (user_id < USER_ID_BASE) return -1;
    off_t offset = (off_t)(user_id - USER_ID_BASE) * sizeof(UserRecord);
    return set_record_lock(fd, type, offset, sizeof(UserRecord), blocking);
}

int unlock_user_record(int fd, int user_id) {
    if (user_id < USER_ID_BASE) return -1;
    off_t offset = (off_t)(user_id - USER_ID_BASE) * sizeof(UserRecord);
    return set_record_lock(fd, LOCK_UNLOCK, offset, sizeof(UserRecord), 0);
}

/* Acquire single-active session for a user */
int acquire_user_session(int user_id, int *session_fd_out) {
    if (user_id < USER_ID_BASE || !session_fd_out) return -1;

    int fd = open(FILE_USERS, O_RDWR, 0644);
    if (fd < 0) return -1;

    off_t offset = (off_t)(user_id - USER_ID_BASE) * sizeof(UserRecord);

    /* Non-blocking write lock test */
    if (set_record_lock(fd, LOCK_WRITE, offset, sizeof(UserRecord), 0) < 0) {
        /* Lock already held by another process! User already logged in */
        close(fd);
        return -1;
    }

    /* Update user record's active status */
    UserRecord u;
    if (lseek(fd, offset, SEEK_SET) != -1 &&
        read(fd, &u, sizeof(UserRecord)) == (ssize_t)sizeof(UserRecord)) {
        u.is_logged_in = SESSION_LOGGED_IN;
        u.session_pid = getpid();
        lseek(fd, offset, SEEK_SET);
        write(fd, &u, sizeof(UserRecord));
    }

    *session_fd_out = fd;
    return 0;
}

/* Release active session */
int release_user_session(int user_id, int session_fd) {
    if (session_fd < 0 || user_id < USER_ID_BASE) return -1;

    off_t offset = (off_t)(user_id - USER_ID_BASE) * sizeof(UserRecord);

    UserRecord u;
    if (lseek(session_fd, offset, SEEK_SET) != -1 &&
        read(session_fd, &u, sizeof(UserRecord)) == (ssize_t)sizeof(UserRecord)) {
        u.is_logged_in = SESSION_LOGGED_OUT;
        u.session_pid = 0;
        lseek(session_fd, offset, SEEK_SET);
        write(session_fd, &u, sizeof(UserRecord));
    }

    unlock_user_record(session_fd, user_id);
    close(session_fd);
    return 0;
}

/* ==================== LOAN LOCKING ==================== */

int lock_loan_record(int fd, int loan_id, RecordLockType type, int blocking) {
    if (loan_id < LOAN_ID_BASE) return -1;
    off_t offset = (off_t)(loan_id - LOAN_ID_BASE) * sizeof(LoanRecord);
    return set_record_lock(fd, type, offset, sizeof(LoanRecord), blocking);
}

int unlock_loan_record(int fd, int loan_id) {
    if (loan_id < LOAN_ID_BASE) return -1;
    off_t offset = (off_t)(loan_id - LOAN_ID_BASE) * sizeof(LoanRecord);
    return set_record_lock(fd, LOCK_UNLOCK, offset, sizeof(LoanRecord), 0);
}
