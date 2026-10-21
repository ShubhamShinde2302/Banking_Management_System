#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <time.h>
#include <sys/types.h>

#define DEFAULT_PORT    8080
#define DEFAULT_HOST    "127.0.0.1"
#define PAYLOAD_SIZE    2048
#define MSG_SIZE        256

/* Request Opcodes */
typedef enum {
    /* Session / Auth */
    REQ_LOGIN                       = 101,
    REQ_LOGOUT                      = 102,
    REQ_CHANGE_PASSWORD             = 103,
    REQ_EXIT                        = 104,

    /* Customer Operations */
    REQ_CUST_VIEW_BALANCE           = 201,
    REQ_CUST_DEPOSIT                = 202,
    REQ_CUST_WITHDRAW               = 203,
    REQ_CUST_TRANSFER               = 204,
    REQ_CUST_APPLY_LOAN             = 205,
    REQ_CUST_ADD_FEEDBACK           = 206,
    REQ_CUST_VIEW_TRANSACTIONS      = 207,

    /* Bank Employee Operations */
    REQ_EMP_ADD_CUSTOMER            = 301,
    REQ_EMP_MODIFY_CUSTOMER         = 302,
    REQ_EMP_VIEW_ASSIGNED_LOANS     = 303,
    REQ_EMP_PROCESS_LOAN            = 304,
    REQ_EMP_VIEW_CUSTOMER_TXNS      = 305,

    /* Manager Operations */
    REQ_MGR_SET_ACCOUNT_STATUS      = 401,
    REQ_MGR_VIEW_UNASSIGNED_LOANS   = 402,
    REQ_MGR_ASSIGN_LOAN             = 403,
    REQ_MGR_VIEW_FEEDBACK           = 404,
    REQ_MGR_LIST_EMPLOYEES          = 405,

    /* Administrator Operations */
    REQ_ADM_ADD_EMPLOYEE            = 501,
    REQ_ADM_MODIFY_DETAILS          = 502,
    REQ_ADM_MANAGE_ROLE             = 503,
    REQ_ADM_LIST_USERS              = 504
} RequestOpcode;

/* Response Status Codes */
typedef enum {
    RESP_OK                         = 200,
    RESP_ERR_BAD_REQUEST            = 400,
    RESP_ERR_AUTH_FAILED            = 401,
    RESP_ERR_UNAUTHORIZED           = 403,
    RESP_ERR_NOT_FOUND              = 404,
    RESP_ERR_ACCOUNT_INACTIVE       = 405,
    RESP_ERR_ALREADY_EXISTS         = 409,
    RESP_ERR_INSUFFICIENT_FUNDS     = 422,
    RESP_ERR_ALREADY_LOGGED_IN      = 423,
    RESP_ERR_RECORD_LOCKED          = 424,
    RESP_ERR_INTERNAL               = 500
} ResponseStatus;

/* Request Network Packet */
typedef struct {
    int opcode;                     /* RequestOpcode */
    int user_id;                    /* Authenticated user ID (or 0 during login) */
    int role;                       /* UserRole */
    char payload[PAYLOAD_SIZE];     /* Serialized or structured request payload */
} RequestPacket;

/* Response Network Packet */
typedef struct {
    int status_code;                /* ResponseStatus */
    char message[MSG_SIZE];         /* Human-readable status message */
    int data_len;                   /* Length of valid data in payload */
    char payload[PAYLOAD_SIZE];     /* Payload response data */
} ResponsePacket;

/* Authentication & Session Payloads */
typedef struct {
    char username[64];
    char password[64];
} LoginPayload;

typedef struct {
    char old_password[64];
    char new_password[64];
} ChangePasswordPayload;

typedef struct {
    int user_id;
    int role;
    int account_id;
    char username[64];
    char customer_name[64];
    double balance;
} AuthSuccessPayload;

#include "common/models.h"

/* Customer Funds Payloads */
typedef struct {
    double amount;
} AmountPayload;

typedef struct {
    int account_id;
    double balance;
    int is_active;
    char customer_name[64];
} BalancePayload;

typedef struct {
    int transaction_id;
    int account_id;
    double amount;
    double balance_after;
    time_t timestamp;
} TransactionResultPayload;

/* Customer Transfer & Passbook Payloads (Day 7) */
typedef struct {
    int target_account_id;
    double amount;
} TransferPayload;

typedef struct {
    int debit_txn_id;
    int credit_txn_id;
    int source_account_id;
    int target_account_id;
    double amount;
    double balance_after;
    time_t timestamp;
} TransferResultPayload;

#define MAX_PASSBOOK_ENTRIES 10

typedef struct {
    int count;
    TransactionRecord records[MAX_PASSBOOK_ENTRIES];
} PassbookPayload;


/* Employee, manager and administrator payloads */
typedef struct { char username[64]; char password[64]; char customer_name[64]; double opening_balance; } CreateCustomerPayload;
typedef struct { int user_id; char customer_name[64]; } ModifyCustomerPayload;
typedef struct { double amount; char reason[128]; } LoanApplicationPayload;
typedef struct { char message[256]; } FeedbackPayload;
typedef struct { int loan_id; int approve; char remarks[128]; } ProcessLoanPayload;
typedef struct { int account_id; int active; } AccountStatusPayload;
typedef struct { int loan_id; int employee_id; } AssignLoanPayload;
typedef struct { int user_id; char username[64]; char customer_name[64]; } ModifyUserPayload;
typedef struct { int user_id; int role; } ManageRolePayload;
typedef struct { int count; UserRecord users[10]; } UserListPayload;
typedef struct { int count; LoanRecord loans[5]; } LoanListPayload;
typedef struct { int count; FeedbackRecord feedback[6]; } FeedbackListPayload;
typedef struct { int count; TransactionRecord records[MAX_PASSBOOK_ENTRIES]; } CustomerTransactionsPayload;
typedef struct { int count; UserRecord employees[10]; } EmployeeListPayload;

#endif /* PROTOCOL_H */
