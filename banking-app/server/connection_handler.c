#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include "server/connection_handler.h"
#include "server/auth.h"
#include "server/customer_ops.h"
#include "server/lock.h"
#include "server/db.h"
#include "common/protocol.h"
#include "common/network.h"


static int require_role(int active_role, int expected, ResponsePacket *res) {
    if (active_role != expected) {
        res->status_code = RESP_ERR_UNAUTHORIZED;
        snprintf(res->message, sizeof(res->message), "This operation is not permitted for your role.");
        return 0;
    }
    return 1;
}

static int create_user_record(const char *username, const char *password, int role,
                              int account_id, UserRecord *out) {
    UserRecord existing;
    if (!username || !*username || !password || strlen(password) < 4 ||
        strlen(username) >= sizeof(existing.username) ||
        strlen(password) >= sizeof(existing.password)) return -1;
    if (db_read_user_by_username(username, &existing) == 0) return -2;
    memset(&existing, 0, sizeof(existing));
    existing.id = db_get_next_user_id();
    strncpy(existing.username, username, sizeof(existing.username)-1);
    strncpy(existing.password, password, sizeof(existing.password)-1);
    existing.role = (UserRole)role;
    existing.account_id = account_id;
    existing.is_active = STATUS_ACTIVE;
    existing.is_logged_in = SESSION_LOGGED_OUT;
    if (db_write_user(&existing) != 0) return -3;
    if (out) *out = existing;
    return 0;
}

static void set_payload(ResponsePacket *res, const void *payload, size_t size) {
    if (size > sizeof(res->payload)) size = sizeof(res->payload);
    memcpy(res->payload, payload, size);
    res->data_len = (int)size;
}

static int create_customer(const CreateCustomerPayload *p, UserRecord *created) {
    if (!p || p->opening_balance < 0 || strlen(p->customer_name) == 0) return RESP_ERR_BAD_REQUEST;
    int account_id = db_get_next_account_id();
    UserRecord u;
    int rc = create_user_record(p->username, p->password, ROLE_CUSTOMER, account_id, &u);
    if (rc == -2) return RESP_ERR_ALREADY_EXISTS;
    if (rc != 0) return RESP_ERR_BAD_REQUEST;
    AccountRecord a;
    memset(&a, 0, sizeof(a));
    a.account_id = account_id;
    a.user_id = u.id;
    strncpy(a.customer_name, p->customer_name, sizeof(a.customer_name)-1);
    a.balance = p->opening_balance;
    a.is_active = STATUS_ACTIVE;
    a.created_at = time(NULL);
    if (db_write_account(&a) != 0) return RESP_ERR_INTERNAL;
    if (p->opening_balance > 0) {
        TransactionRecord t;
        memset(&t, 0, sizeof(t));
        t.transaction_id = db_get_next_txn_id(); t.account_id = account_id;
        t.type = TXN_DEPOSIT; t.amount = p->opening_balance;
        t.balance_after = p->opening_balance; t.timestamp = time(NULL);
        snprintf(t.remarks, sizeof(t.remarks), "Opening balance");
        if (db_write_transaction(&t) != 0) return RESP_ERR_INTERNAL;
    }
    if (created) *created = u;
    return RESP_OK;
}

sem_t *client_slots = NULL;

void handle_client(int client_sock, struct sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    pid_t pid = getpid();

    printf("[WORKER %d] Handling connection from %s:%d\n", pid, client_ip, client_port);

    int active_user_id = 0;
    int active_role = 0;
    int active_account_id = 0;
    int session_fd = -1;

    RequestPacket req;
    ResponsePacket res;

    while (1) {
        memset(&req, 0, sizeof(req));
        int ret = recv_request(client_sock, &req);
        if (ret < 0) {
            printf("[WORKER %d] Client %s:%d disconnected abruptly.\n", pid, client_ip, client_port);
            break;
        }

        memset(&res, 0, sizeof(res));

        switch (req.opcode) {
            case REQ_LOGIN: {
                if (active_user_id > 0) {
                    res.status_code = RESP_ERR_BAD_REQUEST;
                    snprintf(res.message, sizeof(res.message),
                             "Already logged in as User ID %d. Please log out first.", active_user_id);
                } else {
                    LoginPayload *login = (LoginPayload *)req.payload;
                    AuthSuccessPayload auth_info;
                    int status = auth_login(login, &auth_info, &session_fd, res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        active_user_id = auth_info.user_id;
                        active_role = auth_info.role;
                        active_account_id = auth_info.account_id;
                        memcpy(res.payload, &auth_info, sizeof(AuthSuccessPayload));
                        res.data_len = sizeof(AuthSuccessPayload);
                        printf("[WORKER %d] Authenticated %s (ID %d, %s, Acc %d)\n",
                               pid, auth_info.username, auth_info.user_id,
                               role_to_string(auth_info.role), auth_info.account_id);
                    }
                }
                break;
            }

            case REQ_LOGOUT: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "No active session to log out.");
                } else {
                    auth_logout(active_user_id, session_fd);
                    printf("[WORKER %d] User ID %d logged out.\n", pid, active_user_id);
                    active_user_id = 0;
                    active_role = 0;
                    active_account_id = 0;
                    session_fd = -1;
                    res.status_code = RESP_OK;
                    snprintf(res.message, sizeof(res.message), "Logged out successfully.");
                }
                break;
            }

            case REQ_CHANGE_PASSWORD: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to change password.");
                } else {
                    ChangePasswordPayload *cp = (ChangePasswordPayload *)req.payload;
                    int status = auth_change_password(active_user_id, cp, res.message, sizeof(res.message));
                    res.status_code = status;
                }
                break;
            }

            /* ==================== CUSTOMER OPERATIONS (DAY 6) ==================== */
            case REQ_CUST_VIEW_BALANCE: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to view balance.");
                } else if (active_role != ROLE_CUSTOMER) {
                    res.status_code = RESP_ERR_UNAUTHORIZED;
                    snprintf(res.message, sizeof(res.message), "Unauthorized: Customer role required.");
                } else {
                    BalancePayload bal;
                    memset(&bal, 0, sizeof(bal));
                    int status = customer_get_balance(active_user_id, active_account_id, &bal,
                                                      res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        memcpy(res.payload, &bal, sizeof(BalancePayload));
                        res.data_len = sizeof(BalancePayload);
                    }
                }
                break;
            }

            case REQ_CUST_DEPOSIT: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to deposit funds.");
                } else if (active_role != ROLE_CUSTOMER) {
                    res.status_code = RESP_ERR_UNAUTHORIZED;
                    snprintf(res.message, sizeof(res.message), "Unauthorized: Customer role required.");
                } else {
                    AmountPayload *ap = (AmountPayload *)req.payload;
                    TransactionResultPayload txn_res;
                    memset(&txn_res, 0, sizeof(txn_res));
                    int status = customer_deposit(active_user_id, active_account_id, ap->amount,
                                                 &txn_res, res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        memcpy(res.payload, &txn_res, sizeof(TransactionResultPayload));
                        res.data_len = sizeof(TransactionResultPayload);
                        printf("[WORKER %d] Deposit of $%.2f to Acc %d completed (Txn ID: %d)\n",
                               pid, ap->amount, active_account_id, txn_res.transaction_id);
                    }
                }
                break;
            }

            case REQ_CUST_WITHDRAW: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to withdraw funds.");
                } else if (active_role != ROLE_CUSTOMER) {
                    res.status_code = RESP_ERR_UNAUTHORIZED;
                    snprintf(res.message, sizeof(res.message), "Unauthorized: Customer role required.");
                } else {
                    AmountPayload *ap = (AmountPayload *)req.payload;
                    TransactionResultPayload txn_res;
                    memset(&txn_res, 0, sizeof(txn_res));
                    int status = customer_withdraw(active_user_id, active_account_id, ap->amount,
                                                  &txn_res, res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        memcpy(res.payload, &txn_res, sizeof(TransactionResultPayload));
                        res.data_len = sizeof(TransactionResultPayload);
                        printf("[WORKER %d] Withdrawal of $%.2f from Acc %d completed (Txn ID: %d)\n",
                               pid, ap->amount, active_account_id, txn_res.transaction_id);
                    }
                }
                break;
            }

            case REQ_CUST_TRANSFER: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to transfer funds.");
                } else if (active_role != ROLE_CUSTOMER) {
                    res.status_code = RESP_ERR_UNAUTHORIZED;
                    snprintf(res.message, sizeof(res.message), "Unauthorized: Customer role required.");
                } else {
                    TransferPayload *tp = (TransferPayload *)req.payload;
                    TransferResultPayload tr;
                    memset(&tr, 0, sizeof(tr));
                    int status = customer_transfer(active_user_id, active_account_id,
                                                   tp->target_account_id, tp->amount,
                                                   &tr, res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        memcpy(res.payload, &tr, sizeof(TransferResultPayload));
                        res.data_len = sizeof(TransferResultPayload);
                        printf("[WORKER %d] Transfer of $%.2f from Acc %d to Acc %d completed (Debit Txn: %d, Credit Txn: %d)\n",
                               pid, tp->amount, active_account_id, tp->target_account_id,
                               tr.debit_txn_id, tr.credit_txn_id);
                    }
                }
                break;
            }

            case REQ_CUST_VIEW_TRANSACTIONS: {
                if (active_user_id == 0) {
                    res.status_code = RESP_ERR_AUTH_FAILED;
                    snprintf(res.message, sizeof(res.message), "Authentication required to view passbook.");
                } else if (active_role != ROLE_CUSTOMER) {
                    res.status_code = RESP_ERR_UNAUTHORIZED;
                    snprintf(res.message, sizeof(res.message), "Unauthorized: Customer role required.");
                } else {
                    PassbookPayload pb;
                    memset(&pb, 0, sizeof(pb));
                    int status = customer_get_passbook(active_user_id, active_account_id,
                                                      &pb, res.message, sizeof(res.message));
                    res.status_code = status;
                    if (status == RESP_OK) {
                        memcpy(res.payload, &pb, sizeof(PassbookPayload));
                        res.data_len = sizeof(PassbookPayload);
                        printf("[WORKER %d] Returned %d passbook transactions for Acc %d\n",
                               pid, pb.count, active_account_id);
                    }
                }
                break;
            }


            case REQ_CUST_APPLY_LOAN: {
                if (active_user_id == 0) { res.status_code=RESP_ERR_AUTH_FAILED; snprintf(res.message,sizeof(res.message),"Login required."); }
                else if (!require_role(active_role, ROLE_CUSTOMER, &res)) { }
                else {
                    LoanApplicationPayload *p=(LoanApplicationPayload *)req.payload;
                    if (p->amount <= 0 || !p->reason[0]) { res.status_code=RESP_ERR_BAD_REQUEST; snprintf(res.message,sizeof(res.message),"Enter a positive amount and purpose."); }
                    else {
                        LoanRecord l; memset(&l,0,sizeof(l)); l.loan_id=db_get_next_loan_id(); l.customer_id=active_user_id;
                        l.account_id=active_account_id; l.amount=p->amount; l.status=LOAN_PENDING; l.applied_at=time(NULL);
                        strncpy(l.reason,p->reason,sizeof(l.reason)-1);
                        if(db_write_loan(&l)==0){res.status_code=RESP_OK; snprintf(res.message,sizeof(res.message),"Loan application submitted. Loan ID: %d",l.loan_id); set_payload(&res,&l,sizeof(l));}
                        else {res.status_code=RESP_ERR_INTERNAL; snprintf(res.message,sizeof(res.message),"Could not save loan application.");}
                    }
                } break;
            }
            case REQ_CUST_ADD_FEEDBACK: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_CUSTOMER,&res)){}
                else {
                    FeedbackPayload *p=(FeedbackPayload *)req.payload; UserRecord u; FeedbackRecord f; memset(&f,0,sizeof(f));
                    if(!p->message[0]){res.status_code=RESP_ERR_BAD_REQUEST;snprintf(res.message,sizeof(res.message),"Feedback cannot be empty.");}
                    else if(db_read_user_by_id(active_user_id,&u)!=0){res.status_code=RESP_ERR_NOT_FOUND;snprintf(res.message,sizeof(res.message),"Customer not found.");}
                    else {f.feedback_id=db_get_next_feedback_id();f.customer_id=active_user_id;strncpy(f.customer_name,u.username,sizeof(f.customer_name)-1);strncpy(f.message,p->message,sizeof(f.message)-1);f.submitted_at=time(NULL);
                        res.status_code=db_write_feedback(&f)==0?RESP_OK:RESP_ERR_INTERNAL;snprintf(res.message,sizeof(res.message),res.status_code==RESP_OK?"Feedback submitted successfully.":"Could not save feedback.");}
                } break;
            }
            case REQ_EMP_ADD_CUSTOMER: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_EMPLOYEE,&res)){}
                else {CreateCustomerPayload *p=(CreateCustomerPayload *)req.payload;UserRecord u;int st=create_customer(p,&u);res.status_code=st;
                    if(st==RESP_OK){snprintf(res.message,sizeof(res.message),"Customer created. User ID: %d, Account ID: %d",u.id,u.account_id);set_payload(&res,&u,sizeof(u));}
                    else snprintf(res.message,sizeof(res.message),st==RESP_ERR_ALREADY_EXISTS?"Username already exists.":st==RESP_ERR_BAD_REQUEST?"Invalid customer details/password/balance.":"Customer creation failed.");}
                break;
            }
            case REQ_EMP_MODIFY_CUSTOMER: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_EMPLOYEE,&res)){}
                else {ModifyCustomerPayload *p=(ModifyCustomerPayload *)req.payload;UserRecord u;AccountRecord a;
                    if(db_read_user_by_id(p->user_id,&u)!=0||u.role!=ROLE_CUSTOMER||db_read_account_by_user_id(p->user_id,&a)!=0){res.status_code=RESP_ERR_NOT_FOUND;snprintf(res.message,sizeof(res.message),"Customer not found.");}
                    else if(!p->customer_name[0]){res.status_code=RESP_ERR_BAD_REQUEST;snprintf(res.message,sizeof(res.message),"Name cannot be empty.");}
                    else {strncpy(a.customer_name,p->customer_name,sizeof(a.customer_name)-1);res.status_code=db_update_account(&a)==0?RESP_OK:RESP_ERR_INTERNAL;snprintf(res.message,sizeof(res.message),res.status_code==RESP_OK?"Customer details updated.":"Could not update customer.");}}
                break;
            }
            case REQ_EMP_VIEW_ASSIGNED_LOANS: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_EMPLOYEE,&res)){}
                else {LoanRecord list[5];int n=db_get_loans_by_employee(active_user_id,list,5);LoanListPayload p;memset(&p,0,sizeof(p));p.count=n;memcpy(p.loans,list,(size_t)n*sizeof(LoanRecord));res.status_code=RESP_OK;snprintf(res.message,sizeof(res.message),"Found %d assigned loan(s).",n);set_payload(&res,&p,sizeof(p));}
                break;
            }
            case REQ_EMP_PROCESS_LOAN: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_EMPLOYEE,&res)){}
                else {ProcessLoanPayload *p=(ProcessLoanPayload *)req.payload;LoanRecord l;
                    if(db_read_loan_by_id(p->loan_id,&l)!=0){res.status_code=RESP_ERR_NOT_FOUND;snprintf(res.message,sizeof(res.message),"Loan not found.");}
                    else if(l.assigned_employee_id!=active_user_id||l.status!=LOAN_ASSIGNED){res.status_code=RESP_ERR_UNAUTHORIZED;snprintf(res.message,sizeof(res.message),"Loan is not assigned to you or already processed.");}
                    else {l.status=p->approve?LOAN_APPROVED:LOAN_REJECTED;l.processed_at=time(NULL);strncpy(l.remarks,p->remarks,sizeof(l.remarks)-1);res.status_code=db_update_loan(&l)==0?RESP_OK:RESP_ERR_INTERNAL;snprintf(res.message,sizeof(res.message),res.status_code==RESP_OK?"Loan decision saved.":"Could not update loan.");}}
                break;
            }
            case REQ_EMP_VIEW_CUSTOMER_TXNS: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_EMPLOYEE,&res)){}
                else {int *id=(int *)req.payload;AccountRecord a;CustomerTransactionsPayload p;memset(&p,0,sizeof(p));
                    if(db_read_account_by_id(*id,&a)!=0){res.status_code=RESP_ERR_NOT_FOUND;snprintf(res.message,sizeof(res.message),"Account not found.");}
                    else {p.count=db_get_transactions_by_account(*id,p.records,MAX_PASSBOOK_ENTRIES);res.status_code=RESP_OK;snprintf(res.message,sizeof(res.message),"Passbook for account %d: %d transaction(s).",*id,p.count);set_payload(&res,&p,sizeof(p));}}
                break;
            }
            case REQ_MGR_SET_ACCOUNT_STATUS: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_MANAGER,&res)){}
                else {AccountStatusPayload *p=(AccountStatusPayload *)req.payload;AccountRecord a;UserRecord u;
                    if(db_read_account_by_id(p->account_id,&a)!=0||db_read_user_by_id(a.user_id,&u)!=0){res.status_code=RESP_ERR_NOT_FOUND;snprintf(res.message,sizeof(res.message),"Account not found.");}
                    else {a.is_active=p->active?STATUS_ACTIVE:STATUS_INACTIVE;u.is_active=a.is_active;int ok=db_update_account(&a)==0&&db_update_user(&u)==0;res.status_code=ok?RESP_OK:RESP_ERR_INTERNAL;snprintf(res.message,sizeof(res.message),ok?"Account status updated.":"Could not update account status.");}}
                break;
            }
            case REQ_MGR_VIEW_UNASSIGNED_LOANS: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_MANAGER,&res)){}
                else {LoanRecord all[128];int n=db_get_all_loans(all,128);LoanListPayload p;memset(&p,0,sizeof(p));for(int i=0;i<n&&p.count<5;i++)if(all[i].status==LOAN_PENDING)p.loans[p.count++]=all[i];res.status_code=RESP_OK;snprintf(res.message,sizeof(res.message),"Found %d pending loan(s) (showing up to 5).",p.count);set_payload(&res,&p,sizeof(p));}
                break;
            }
            case REQ_MGR_ASSIGN_LOAN: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_MANAGER,&res)){}
                else {AssignLoanPayload *p=(AssignLoanPayload *)req.payload;LoanRecord l;UserRecord e;
                    if(db_read_loan_by_id(p->loan_id,&l)!=0){res.status_code=RESP_ERR_NOT_FOUND;snprintf(res.message,sizeof(res.message),"Loan not found.");}
                    else if(db_read_user_by_id(p->employee_id,&e)!=0||e.role!=ROLE_EMPLOYEE||!e.is_active){res.status_code=RESP_ERR_BAD_REQUEST;snprintf(res.message,sizeof(res.message),"Active employee ID required.");}
                    else if(l.status!=LOAN_PENDING){res.status_code=RESP_ERR_BAD_REQUEST;snprintf(res.message,sizeof(res.message),"Only pending loans can be assigned.");}
                    else {l.assigned_employee_id=p->employee_id;l.status=LOAN_ASSIGNED;res.status_code=db_update_loan(&l)==0?RESP_OK:RESP_ERR_INTERNAL;snprintf(res.message,sizeof(res.message),res.status_code==RESP_OK?"Loan assigned to employee.":"Loan assignment failed.");}}
                break;
            }
            case REQ_MGR_VIEW_FEEDBACK: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_MANAGER,&res)){}
                else {FeedbackRecord all[64];int n=db_get_all_feedbacks(all,64);FeedbackListPayload p;memset(&p,0,sizeof(p));int start=n>6?n-6:0;for(int i=start;i<n&&p.count<6;i++)p.feedback[p.count++]=all[i];res.status_code=RESP_OK;snprintf(res.message,sizeof(res.message),"Showing %d recent feedback item(s).",p.count);set_payload(&res,&p,sizeof(p));}
                break;
            }
            case REQ_MGR_LIST_EMPLOYEES: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_MANAGER,&res)){}
                else {UserRecord all[64];int n=db_get_all_users(all,64);EmployeeListPayload p;memset(&p,0,sizeof(p));for(int i=0;i<n&&p.count<10;i++)if(all[i].role==ROLE_EMPLOYEE&&all[i].is_active)p.employees[p.count++]=all[i];res.status_code=RESP_OK;snprintf(res.message,sizeof(res.message),"Found %d active employee(s).",p.count);set_payload(&res,&p,sizeof(p));}
                break;
            }
            case REQ_ADM_ADD_EMPLOYEE: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_ADMIN,&res)){}
                else {CreateCustomerPayload *p=(CreateCustomerPayload *)req.payload;UserRecord u;memset(&u,0,sizeof(u));int rc=create_user_record(p->username,p->password,ROLE_EMPLOYEE,0,&u);res.status_code=rc==0?RESP_OK:rc==-2?RESP_ERR_ALREADY_EXISTS:RESP_ERR_BAD_REQUEST;if(rc==0)snprintf(res.message,sizeof(res.message),"Employee created. User ID: %d",u.id);else snprintf(res.message,sizeof(res.message),rc==-2?"Username already exists.":"Invalid username or password.");if(rc==0)set_payload(&res,&u,sizeof(u));}
                break;
            }
            case REQ_ADM_MODIFY_DETAILS: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_ADMIN,&res)){}
                else {ModifyUserPayload *p=(ModifyUserPayload *)req.payload;UserRecord u;
                    if(db_read_user_by_id(p->user_id,&u)!=0){res.status_code=RESP_ERR_NOT_FOUND;snprintf(res.message,sizeof(res.message),"User not found.");}
                    else {if(p->username[0]){UserRecord existing;if(db_read_user_by_username(p->username,&existing)==0&&existing.id!=u.id){res.status_code=RESP_ERR_ALREADY_EXISTS;snprintf(res.message,sizeof(res.message),"Username already in use.");break;}strncpy(u.username,p->username,sizeof(u.username)-1);}if(db_update_user(&u)!=0){res.status_code=RESP_ERR_INTERNAL;snprintf(res.message,sizeof(res.message),"Could not update user.");}else if(u.role==ROLE_CUSTOMER&&p->customer_name[0]){AccountRecord a;if(db_read_account_by_user_id(u.id,&a)==0){strncpy(a.customer_name,p->customer_name,sizeof(a.customer_name)-1);db_update_account(&a);}res.status_code=RESP_OK;snprintf(res.message,sizeof(res.message),"User and customer details updated.");}else{res.status_code=RESP_OK;snprintf(res.message,sizeof(res.message),"User details updated.");}}}
                break;
            }
            case REQ_ADM_MANAGE_ROLE: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_ADMIN,&res)){}
                else {ManageRolePayload *p=(ManageRolePayload *)req.payload;UserRecord u;
                    if(p->role<ROLE_CUSTOMER||p->role>ROLE_ADMIN||db_read_user_by_id(p->user_id,&u)!=0){res.status_code=RESP_ERR_BAD_REQUEST;snprintf(res.message,sizeof(res.message),"Invalid user ID or role.");}
                    else if(u.id==active_user_id&&p->role!=ROLE_ADMIN){res.status_code=RESP_ERR_BAD_REQUEST;snprintf(res.message,sizeof(res.message),"You cannot remove your own administrator role.");}
                    else {u.role=(UserRole)p->role;res.status_code=db_update_user(&u)==0?RESP_OK:RESP_ERR_INTERNAL;snprintf(res.message,sizeof(res.message),res.status_code==RESP_OK?"User role updated.":"Role update failed.");}}
                break;
            }
            case REQ_ADM_LIST_USERS: {
                if(active_user_id==0){res.status_code=RESP_ERR_AUTH_FAILED;snprintf(res.message,sizeof(res.message),"Login required.");}
                else if(!require_role(active_role,ROLE_ADMIN,&res)){}
                else {UserRecord all[10];UserListPayload p;memset(&p,0,sizeof(p));p.count=db_get_all_users(all,10);memcpy(p.users,all,(size_t)p.count*sizeof(UserRecord));res.status_code=RESP_OK;snprintf(res.message,sizeof(res.message),"Showing %d user(s).",p.count);set_payload(&res,&p,sizeof(p));}
                break;
            }

            case REQ_EXIT: {
                if (active_user_id > 0 && session_fd >= 0) {
                    auth_logout(active_user_id, session_fd);
                    active_user_id = 0;
                    active_role = 0;
                    active_account_id = 0;
                    session_fd = -1;
                }
                res.status_code = RESP_OK;
                snprintf(res.message, sizeof(res.message), "Session closed. Goodbye!");
                send_response(client_sock, &res);
                printf("[WORKER %d] Client %s:%d requested exit.\n", pid, client_ip, client_port);
                goto cleanup;
            }

            case 100: { /* Initial connection probe / ping */
                res.status_code = RESP_OK;
                snprintf(res.message, sizeof(res.message), "Server Worker PID %d ready.", pid);
                snprintf(res.payload, sizeof(res.payload), "ACK: Handshake probe successful.");
                res.data_len = strlen(res.payload);
                break;
            }

            default: {
                res.status_code = RESP_ERR_BAD_REQUEST;
                snprintf(res.message, sizeof(res.message), "Unsupported request opcode: %d", req.opcode);
                break;
            }
        }

        if (send_response(client_sock, &res) < 0) {
            printf("[WORKER %d] Failed to send response to %s:%d\n", pid, client_ip, client_port);
            break;
        }
    }

cleanup:
    /* Safety clean-up on exit or abrupt crash */
    if (active_user_id > 0 && session_fd >= 0) {
        auth_logout(active_user_id, session_fd);
    }

    close(client_sock);
    if (client_slots && client_slots != SEM_FAILED) sem_post(client_slots);
    printf("[WORKER %d] Connection with %s:%d closed cleanly. Exiting child process.\n", pid, client_ip, client_port);
    return;
}
