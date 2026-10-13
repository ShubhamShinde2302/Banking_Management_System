#ifndef DB_H
#define DB_H

#include "common/models.h"

#define DATA_DIR            "data"
#define FILE_USERS          "data/users.dat"
#define FILE_ACCOUNTS       "data/accounts.dat"
#define FILE_TRANSACTIONS   "data/transactions.dat"
#define FILE_LOANS          "data/loans.dat"
#define FILE_FEEDBACK       "data/feedback.dat"

#define USER_ID_BASE        1001
#define ACCOUNT_ID_BASE     2001
#define TXN_ID_BASE         5001
#define LOAN_ID_BASE        8001
#define FEEDBACK_ID_BASE    9001

/* Initialization */
int db_init(void);

/* User Operations */
int db_get_next_user_id(void);
int db_write_user(const UserRecord *user);
int db_read_user_by_id(int user_id, UserRecord *out);
int db_read_user_by_username(const char *username, UserRecord *out);
int db_update_user(const UserRecord *user);
int db_get_all_users(UserRecord *out_list, int max_count);

/* Account Operations */
int db_get_next_account_id(void);
int db_write_account(const AccountRecord *acc);
int db_read_account_by_id(int account_id, AccountRecord *out);
int db_read_account_by_user_id(int user_id, AccountRecord *out);
int db_update_account(const AccountRecord *acc);
int db_get_all_accounts(AccountRecord *out_list, int max_count);

/* Transaction Operations */
int db_get_next_txn_id(void);
int db_write_transaction(const TransactionRecord *txn);
int db_get_transactions_by_account(int account_id, TransactionRecord *out_list, int max_count);

/* Loan Operations */
int db_get_next_loan_id(void);
int db_write_loan(const LoanRecord *loan);
int db_read_loan_by_id(int loan_id, LoanRecord *out);
int db_update_loan(const LoanRecord *loan);
int db_get_all_loans(LoanRecord *out_list, int max_count);
int db_get_loans_by_employee(int employee_id, LoanRecord *out_list, int max_count);

/* Feedback Operations */
int db_get_next_feedback_id(void);
int db_write_feedback(const FeedbackRecord *fb);
int db_get_all_feedbacks(FeedbackRecord *out_list, int max_count);

#endif /* DB_H */
