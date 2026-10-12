#ifndef MODELS_H
#define MODELS_H

#include <time.h>
#include <unistd.h>

/* Role Enumerations */
typedef enum {
    ROLE_CUSTOMER = 1,
    ROLE_EMPLOYEE = 2,
    ROLE_MANAGER  = 3,
    ROLE_ADMIN    = 4
} UserRole;

/* Account & User Status */
typedef enum {
    STATUS_INACTIVE = 0,
    STATUS_ACTIVE   = 1
} AccountStatus;

/* Session Status */
typedef enum {
    SESSION_LOGGED_OUT = 0,
    SESSION_LOGGED_IN  = 1
} SessionStatus;

/* Transaction Types */
typedef enum {
    TXN_DEPOSIT         = 1,
    TXN_WITHDRAW        = 2,
    TXN_TRANSFER_DEBIT  = 3,
    TXN_TRANSFER_CREDIT = 4
} TransactionType;

/* Loan Status */
typedef enum {
    LOAN_PENDING  = 0,
    LOAN_ASSIGNED = 1,
    LOAN_APPROVED = 2,
    LOAN_REJECTED = 3
} LoanStatus;

/* User Record Structure */
typedef struct {
    int id;                     /* Unique User ID */
    char username[64];          /* Login username */
    char password[64];          /* Password (plaintext or hashed) */
    UserRole role;              /* Customer, Employee, Manager, Admin */
    int account_id;             /* Associated Account ID (for customers; 0 otherwise) */
    int is_active;              /* 1 = active, 0 = deactivated */
    int is_logged_in;           /* 1 = currently logged in (one-session constraint) */
    pid_t session_pid;          /* PID of client handler holding the session */
} UserRecord;

/* Bank Account Structure */
typedef struct {
    int account_id;             /* Unique Account ID */
    int user_id;                /* Owner Customer ID */
    char customer_name[64];     /* Account holder's display name */
    double balance;             /* Current available balance */
    int is_active;              /* 1 = active, 0 = frozen / deactivated */
    time_t created_at;          /* Account creation timestamp */
} AccountRecord;

/* Transaction History Structure */
typedef struct {
    int transaction_id;         /* Unique Transaction ID */
    int account_id;             /* Primary Account ID */
    int target_account_id;      /* Counterparty Account ID (for transfers; 0 for deposit/withdraw) */
    TransactionType type;       /* Deposit, Withdraw, Transfer Debit/Credit */
    double amount;              /* Transaction amount */
    double balance_after;       /* Balance after transaction */
    time_t timestamp;           /* Epoch timestamp of the transaction */
    char remarks[128];          /* Description / Note */
} TransactionRecord;

/* Loan Application Structure */
typedef struct {
    int loan_id;                /* Unique Loan ID */
    int customer_id;            /* Applicant Customer ID */
    int account_id;             /* Target Account ID for disbursement */
    double amount;              /* Requested loan amount */
    int assigned_employee_id;   /* Assigned Bank Employee ID (0 if not yet assigned) */
    LoanStatus status;          /* Pending, Assigned, Approved, Rejected */
    char reason[128];           /* Purpose of loan submitted by customer */
    char remarks[128];          /* Decision notes from employee/manager */
    time_t applied_at;          /* Timestamp of application */
    time_t processed_at;        /* Timestamp of approval/rejection */
} LoanRecord;

/* Feedback Structure */
typedef struct {
    int feedback_id;            /* Unique Feedback ID */
    int customer_id;            /* Submitting Customer ID */
    char customer_name[64];     /* Customer Name */
    char message[256];          /* Feedback text */
    time_t submitted_at;        /* Timestamp */
} FeedbackRecord;

#endif /* MODELS_H */
