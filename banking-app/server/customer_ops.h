#ifndef CUSTOMER_OPS_H
#define CUSTOMER_OPS_H

#include <stddef.h>
#include "common/models.h"
#include "common/protocol.h"

/* Get balance for an account with advisory read record lock */
int customer_get_balance(int user_id,
                         int account_id,
                         BalancePayload *out_bal,
                         char *msg_out,
                         size_t msg_len);

/* Deposit funds into an account with exclusive write record lock & txn recording */
int customer_deposit(int user_id,
                     int account_id,
                     double amount,
                     TransactionResultPayload *out_res,
                     char *msg_out,
                     size_t msg_len);

/* Withdraw funds from an account with exclusive write record lock & txn recording */
int customer_withdraw(int user_id,
                      int account_id,
                      double amount,
                      TransactionResultPayload *out_res,
                      char *msg_out,
                      size_t msg_len);

#endif /* CUSTOMER_OPS_H */
