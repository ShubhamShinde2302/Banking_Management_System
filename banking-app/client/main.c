#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common/models.h"
#include "common/protocol.h"
#include "common/network.h"

static void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {}
}

static const char *get_role_label(int role) {
    switch (role) {
        case ROLE_CUSTOMER: return "Customer";
        case ROLE_EMPLOYEE: return "Bank Employee";
        case ROLE_MANAGER:  return "Bank Manager";
        case ROLE_ADMIN:    return "Administrator";
        default:            return "Unknown";
    }
}

static void show_about_accounts(void) {
    printf("\n======================================================\n");
    printf("   Pre-seeded Test Accounts (Default Credentials)     \n");
    printf("======================================================\n");
    printf(" 1. Administrator:  Username='admin'     Password='adminpassword'\n");
    printf(" 2. Bank Manager:   Username='manager1'  Password='managerpassword'\n");
    printf(" 3. Bank Employee:  Username='employee1' Password='employeepassword'\n");
    printf(" 4. Customer Alice: Username='alice'     Password='alicepassword' (Acc: 2001, $15k)\n");
    printf(" 5. Customer Bob:   Username='bob'       Password='bobpassword'   (Acc: 2002, $8k)\n");
    printf("======================================================\n\n");
}

/* ==================== CUSTOMER MENU (DAY 7) ==================== */
static void handle_customer_session(int sock, AuthSuccessPayload *auth) {
    RequestPacket req;
    ResponsePacket res;
    int choice = 0;

    while (1) {
        printf("\n======================================================\n");
        printf("   CUSTOMER PORTAL: %s\n", auth->customer_name);
        printf("   Account #: %-8d | User ID: %-6d | Balance: $%.2f\n",
               auth->account_id, auth->user_id, auth->balance);
        printf("======================================================\n");
        printf(" 1. View Account Balance (Live Query)\n");
        printf(" 2. Deposit Funds\n");
        printf(" 3. Withdraw Funds\n");
        printf(" 4. Transfer Funds (Atomic 2-Phase Lock)\n");
        printf(" 5. View Passbook / Recent Transactions\n");
        printf(" 6. Change Password\n");
        printf(" 7. View Account & Session Details\n");
        printf(" 8. Send Handshake Probe (Ping Server)\n");
        printf(" 9. Apply for a Loan\n");
        printf("10. Add Feedback\n");
        printf("11. Logout\n");
        printf("Enter your choice (1-11): ");

        if (scanf("%d", &choice) != 1) {
            clear_input_buffer();
            break;
        }
        clear_input_buffer();

        if (choice == 1) {
            /* Live Balance Query */
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CUST_VIEW_BALANCE;
            req.user_id = auth->user_id;
            req.role = auth->role;

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Error communicating with server.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                BalancePayload bal;
                memcpy(&bal, res.payload, sizeof(bal));
                auth->balance = bal.balance; /* update local cache */

                printf("\n------------------------------------------------------\n");
                printf("                ACCOUNT BALANCE DETAILS               \n");
                printf("------------------------------------------------------\n");
                printf(" Account Number:    %d\n", bal.account_id);
                printf(" Account Holder:    %s\n", bal.customer_name);
                printf(" Account Status:    %s\n", bal.is_active ? "ACTIVE" : "FROZEN");
                printf(" Available Balance: $%.2f\n", bal.balance);
                printf("------------------------------------------------------\n");
            } else {
                printf("\n[-] Failed to retrieve balance [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 2) {
            /* Deposit Funds */
            double amount = 0.0;
            printf("\n--- Deposit Funds ---\n");
            printf("Enter amount to deposit ($): ");
            if (scanf("%lf", &amount) != 1) {
                clear_input_buffer();
                printf("[-] Invalid amount entered.\n");
                continue;
            }
            clear_input_buffer();

            if (amount <= 0.0) {
                printf("[-] Deposit amount must be greater than zero.\n");
                continue;
            }

            AmountPayload ap;
            ap.amount = amount;

            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CUST_DEPOSIT;
            req.user_id = auth->user_id;
            req.role = auth->role;
            memcpy(req.payload, &ap, sizeof(ap));

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Network error during deposit.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                TransactionResultPayload tr;
                memcpy(&tr, res.payload, sizeof(tr));
                auth->balance = tr.balance_after; /* update cached balance */

                printf("\n======================================================\n");
                printf("   [+] DEPOSIT SUCCESSFUL!\n");
                printf("   Transaction ID:   %d\n", tr.transaction_id);
                printf("   Credited Amount:  $%.2f\n", tr.amount);
                printf("   Updated Balance:  $%.2f\n", tr.balance_after);
                printf("======================================================\n");
            } else {
                printf("\n[-] Deposit Failed [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 3) {
            /* Withdraw Funds */
            double amount = 0.0;
            printf("\n--- Withdraw Funds ---\n");
            printf("Enter amount to withdraw ($): ");
            if (scanf("%lf", &amount) != 1) {
                clear_input_buffer();
                printf("[-] Invalid amount entered.\n");
                continue;
            }
            clear_input_buffer();

            if (amount <= 0.0) {
                printf("[-] Withdrawal amount must be greater than zero.\n");
                continue;
            }

            AmountPayload ap;
            ap.amount = amount;

            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CUST_WITHDRAW;
            req.user_id = auth->user_id;
            req.role = auth->role;
            memcpy(req.payload, &ap, sizeof(ap));

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Network error during withdrawal.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                TransactionResultPayload tr;
                memcpy(&tr, res.payload, sizeof(tr));
                auth->balance = tr.balance_after; /* update cached balance */

                printf("\n======================================================\n");
                printf("   [+] WITHDRAWAL SUCCESSFUL!\n");
                printf("   Transaction ID:   %d\n", tr.transaction_id);
                printf("   Debited Amount:   $%.2f\n", tr.amount);
                printf("   Remaining Balance: $%.2f\n", tr.balance_after);
                printf("======================================================\n");
            } else {
                printf("\n[-] Withdrawal Failed [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 4) {
            /* Transfer Funds (Day 7) */
            int target_acc = 0;
            double amount = 0.0;

            printf("\n--- Transfer Funds (Inter-Account) ---\n");
            printf("Enter Destination Account Number (e.g. 2002): ");
            if (scanf("%d", &target_acc) != 1) {
                clear_input_buffer();
                printf("[-] Invalid account number entered.\n");
                continue;
            }
            printf("Enter Amount to Transfer ($): ");
            if (scanf("%lf", &amount) != 1) {
                clear_input_buffer();
                printf("[-] Invalid transfer amount entered.\n");
                continue;
            }
            clear_input_buffer();

            if (amount <= 0.0) {
                printf("[-] Transfer amount must be greater than zero.\n");
                continue;
            }

            TransferPayload tp;
            tp.target_account_id = target_acc;
            tp.amount = amount;

            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CUST_TRANSFER;
            req.user_id = auth->user_id;
            req.role = auth->role;
            memcpy(req.payload, &tp, sizeof(tp));

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Network error during transfer.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                TransferResultPayload tr;
                memcpy(&tr, res.payload, sizeof(tr));
                auth->balance = tr.balance_after; /* update cached balance */

                printf("\n======================================================\n");
                printf("   [+] TRANSFER COMPLETED SUCCESSFULLY!\n");
                printf("   Debit Txn ID:     %d\n", tr.debit_txn_id);
                printf("   Credit Txn ID:    %d\n", tr.credit_txn_id);
                printf("   Transferred:      $%.2f\n", tr.amount);
                printf("   Destination Acc:  #%d\n", tr.target_account_id);
                printf("   Updated Balance:  $%.2f\n", tr.balance_after);
                printf("======================================================\n");
            } else {
                printf("\n[-] Transfer Failed [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 5) {
            /* View Passbook (Day 7) */
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CUST_VIEW_TRANSACTIONS;
            req.user_id = auth->user_id;
            req.role = auth->role;

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Network error fetching passbook.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                PassbookPayload pb;
                memcpy(&pb, res.payload, sizeof(pb));

                printf("\n======================================================================================================\n");
                printf("                         PASSBOOK / TRANSACTION HISTORY (Account #%d)                         \n", auth->account_id);
                printf("======================================================================================================\n");
                printf(" %-8s | %-16s | %-16s | %-10s | %-14s | %-14s | %-24s\n",
                       "Txn ID", "Date & Time", "Type", "Target Acc", "Amount", "Balance After", "Description");
                printf("------------------------------------------------------------------------------------------------------\n");

                if (pb.count == 0) {
                    printf(" [No transaction records found for this account]\n");
                } else {
                    for (int i = 0; i < pb.count; ++i) {
                        TransactionRecord *t = &pb.records[i];
                        char time_str[32] = {0};
                        struct tm *tm_info = localtime(&t->timestamp);
                        if (tm_info) {
                            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
                        } else {
                            strncpy(time_str, "N/A", sizeof(time_str) - 1);
                        }

                        const char *type_name = "UNKNOWN";
                        char amt_str[32] = {0};
                        if (t->type == TXN_DEPOSIT) {
                            type_name = "DEPOSIT";
                            snprintf(amt_str, sizeof(amt_str), "+$%.2f", t->amount);
                        } else if (t->type == TXN_WITHDRAW) {
                            type_name = "WITHDRAW";
                            snprintf(amt_str, sizeof(amt_str), "-$%.2f", t->amount);
                        } else if (t->type == TXN_TRANSFER_DEBIT) {
                            type_name = "XFER_DEBIT";
                            snprintf(amt_str, sizeof(amt_str), "-$%.2f", t->amount);
                        } else if (t->type == TXN_TRANSFER_CREDIT) {
                            type_name = "XFER_CREDIT";
                            snprintf(amt_str, sizeof(amt_str), "+$%.2f", t->amount);
                        }

                        char target_str[16] = "-";
                        if (t->target_account_id > 0) {
                            snprintf(target_str, sizeof(target_str), "#%d", t->target_account_id);
                        }

                        printf(" %-8d | %-16s | %-16s | %-10s | %-14s | $%-13.2f | %-24s\n",
                               t->transaction_id, time_str, type_name, target_str,
                               amt_str, t->balance_after, t->remarks);
                    }
                }
                printf("======================================================================================================\n");
            } else {
                printf("\n[-] Failed to fetch passbook [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 6) {
            /* Change Password */
            char old_pwd[64] = {0};
            char new_pwd[64] = {0};
            char confirm_pwd[64] = {0};

            printf("\n--- Change Password ---\n");
            printf("Enter current password: ");
            if (scanf("%63s", old_pwd) != 1) { clear_input_buffer(); continue; }
            printf("Enter new password (min 4 chars): ");
            if (scanf("%63s", new_pwd) != 1) { clear_input_buffer(); continue; }
            printf("Confirm new password: ");
            if (scanf("%63s", confirm_pwd) != 1) { clear_input_buffer(); continue; }
            clear_input_buffer();

            if (strcmp(new_pwd, confirm_pwd) != 0) {
                printf("\n[-] Error: New password and confirmation do not match!\n");
                continue;
            }

            ChangePasswordPayload cp;
            memset(&cp, 0, sizeof(cp));
            strncpy(cp.old_password, old_pwd, sizeof(cp.old_password) - 1);
            strncpy(cp.new_password, new_pwd, sizeof(cp.new_password) - 1);

            memset(&req, 0, sizeof(req));
            req.opcode = REQ_CHANGE_PASSWORD;
            req.user_id = auth->user_id;
            req.role = auth->role;
            memcpy(req.payload, &cp, sizeof(cp));

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Network error communicating with server.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                printf("\n[+] SUCCESS: %s\n", res.message);
            } else {
                printf("\n[-] FAILED [%d]: %s\n", res.status_code, res.message);
            }

        } else if (choice == 7) {
            /* Session & Account Details */
            printf("\n======================================================\n");
            printf("               ACCOUNT & SESSION DETAILS              \n");
            printf("======================================================\n");
            printf(" User ID:        %d\n", auth->user_id);
            printf(" Username:       %s\n", auth->username);
            printf(" Customer Name:  %s\n", auth->customer_name);
            printf(" Role:           Customer\n");
            printf(" Primary Acc #:  %d\n", auth->account_id);
            printf(" Cached Balance: $%.2f\n", auth->balance);
            printf("======================================================\n");

        } else if (choice == 8) {
            /* Handshake Probe */
            memset(&req, 0, sizeof(req));
            req.opcode = 100;
            req.user_id = auth->user_id;
            req.role = auth->role;
            snprintf(req.payload, sizeof(req.payload), "Ping from user '%s'", auth->username);

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("\n[-] Server disconnected.\n");
                break;
            }
            printf("\n[+] SERVER RESPONSE [%d]: %s\n", res.status_code, res.message);

        } else if (choice == 9) {
            LoanApplicationPayload lp; memset(&lp,0,sizeof(lp));
            printf("Loan amount: $"); if(scanf("%lf",&lp.amount)!=1){clear_input_buffer();continue;}
            clear_input_buffer(); printf("Purpose: "); if(!fgets(lp.reason,sizeof(lp.reason),stdin)) continue;
            lp.reason[strcspn(lp.reason,"\n")]=0;
            memset(&req,0,sizeof(req)); req.opcode=REQ_CUST_APPLY_LOAN; req.user_id=auth->user_id; req.role=auth->role; memcpy(req.payload,&lp,sizeof(lp));
            if(send_request(sock,&req)<0||recv_response(sock,&res)<0){printf("Network error.\n");break;}
            printf("[%d] %s\n",res.status_code,res.message);
        } else if (choice == 10) {
            FeedbackPayload fp; memset(&fp,0,sizeof(fp));
            printf("Enter feedback: "); if(!fgets(fp.message,sizeof(fp.message),stdin)) continue;
            fp.message[strcspn(fp.message,"\n")]=0;
            memset(&req,0,sizeof(req)); req.opcode=REQ_CUST_ADD_FEEDBACK; req.user_id=auth->user_id; req.role=auth->role; memcpy(req.payload,&fp,sizeof(fp));
            if(send_request(sock,&req)<0||recv_response(sock,&res)<0){printf("Network error.\n");break;}
            printf("[%d] %s\n",res.status_code,res.message);
        } else if (choice == 11) {
            /* Logout */
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_LOGOUT;
            req.user_id = auth->user_id;

            if (send_request(sock, &req) == 0 && recv_response(sock, &res) == 0) {
                printf("\n[+] %s\n", res.message);
            } else {
                printf("\n[*] Logged out locally.\n");
            }
            break;
        } else {
            printf("\n[-] Invalid option. Please enter 1-11.\n");
        }
    }
}

/* ==================== ROLE-SPECIFIC STAFF MENUS ==================== */
static int exchange(int sock, RequestPacket *req, ResponsePacket *res) {
    return send_request(sock, req) == 0 && recv_response(sock, res) == 0 ? 0 : -1;
}

static void print_loan_list(const LoanListPayload *p) {
    for (int i=0; i<p->count && i<5; ++i) {
        const LoanRecord *l=&p->loans[i];
        printf("Loan %d | customer %d | account %d | amount %.2f | status %d | assigned employee %d | purpose: %s\n",
               l->loan_id,l->customer_id,l->account_id,l->amount,l->status,l->assigned_employee_id,l->reason);
    }
    if (!p->count) puts("No matching loan applications.");
}

static void handle_staff_session(int sock, AuthSuccessPayload *auth) {
    RequestPacket req; ResponsePacket res; int choice=0;
    while (1) {
        printf("\n======================================================\n");
        printf(" %s PORTAL: %s (User ID %d)\n",get_role_label(auth->role),auth->username,auth->user_id);
        printf("======================================================\n");
        if(auth->role==ROLE_EMPLOYEE) {
            puts("1. Add customer\n2. Modify customer name\n3. View assigned loans\n4. Approve/reject assigned loan\n5. View customer passbook\n6. Change password\n7. Logout");
        } else if(auth->role==ROLE_MANAGER) {
            puts("1. Activate/deactivate account\n2. View pending loans\n3. Assign loan to employee\n4. Review customer feedback\n5. List employees\n6. Change password\n7. Logout");
        } else {
            puts("1. Add bank employee\n2. Modify user details\n3. Manage user role\n4. List users\n5. Change password\n6. Logout");
        }
        printf("Choice: "); if(scanf("%d",&choice)!=1){clear_input_buffer();continue;} clear_input_buffer();
        memset(&req,0,sizeof(req)); memset(&res,0,sizeof(res)); req.user_id=auth->user_id; req.role=auth->role;
        int logout_choice=(auth->role==ROLE_ADMIN)?6:7;
        if(choice==logout_choice){req.opcode=REQ_LOGOUT;if(exchange(sock,&req,&res)==0)printf("%s\n",res.message);break;}
        if(choice==0) break;
        if(auth->role==ROLE_EMPLOYEE) {
            if(choice==1){CreateCustomerPayload p;memset(&p,0,sizeof(p));printf("Username: ");scanf("%63s",p.username);printf("Password (min 4 chars): ");scanf("%63s",p.password);clear_input_buffer();printf("Customer full name: ");fgets(p.customer_name,sizeof(p.customer_name),stdin);p.customer_name[strcspn(p.customer_name,"\n")]=0;printf("Opening balance: $");if(scanf("%lf",&p.opening_balance)!=1){clear_input_buffer();continue;}clear_input_buffer();req.opcode=REQ_EMP_ADD_CUSTOMER;memcpy(req.payload,&p,sizeof(p));}
            else if(choice==2){ModifyCustomerPayload p;memset(&p,0,sizeof(p));printf("Customer user ID: ");scanf("%d",&p.user_id);clear_input_buffer();printf("New customer name: ");fgets(p.customer_name,sizeof(p.customer_name),stdin);p.customer_name[strcspn(p.customer_name,"\n")]=0;req.opcode=REQ_EMP_MODIFY_CUSTOMER;memcpy(req.payload,&p,sizeof(p));}
            else if(choice==3){req.opcode=REQ_EMP_VIEW_ASSIGNED_LOANS;}
            else if(choice==4){ProcessLoanPayload p;memset(&p,0,sizeof(p));printf("Loan ID: ");scanf("%d",&p.loan_id);printf("Decision (1=approve, 0=reject): ");scanf("%d",&p.approve);clear_input_buffer();printf("Remarks: ");fgets(p.remarks,sizeof(p.remarks),stdin);p.remarks[strcspn(p.remarks,"\n")]=0;req.opcode=REQ_EMP_PROCESS_LOAN;memcpy(req.payload,&p,sizeof(p));}
            else if(choice==5){int id=0;printf("Account ID: ");if(scanf("%d",&id)!=1){clear_input_buffer();continue;}clear_input_buffer();req.opcode=REQ_EMP_VIEW_CUSTOMER_TXNS;memcpy(req.payload,&id,sizeof(id));}
            else if(choice==6){ChangePasswordPayload p;memset(&p,0,sizeof(p));printf("Old password: ");scanf("%63s",p.old_password);printf("New password: ");scanf("%63s",p.new_password);clear_input_buffer();req.opcode=REQ_CHANGE_PASSWORD;memcpy(req.payload,&p,sizeof(p));}
            else {puts("Invalid choice.");continue;}
        } else if(auth->role==ROLE_MANAGER) {
            if(choice==1){AccountStatusPayload p;memset(&p,0,sizeof(p));printf("Account ID: ");scanf("%d",&p.account_id);printf("Status (1=active, 0=deactivate): ");scanf("%d",&p.active);clear_input_buffer();req.opcode=REQ_MGR_SET_ACCOUNT_STATUS;memcpy(req.payload,&p,sizeof(p));}
            else if(choice==2){req.opcode=REQ_MGR_VIEW_UNASSIGNED_LOANS;}
            else if(choice==3){AssignLoanPayload p;memset(&p,0,sizeof(p));printf("Loan ID: ");scanf("%d",&p.loan_id);printf("Employee user ID: ");scanf("%d",&p.employee_id);clear_input_buffer();req.opcode=REQ_MGR_ASSIGN_LOAN;memcpy(req.payload,&p,sizeof(p));}
            else if(choice==4){req.opcode=REQ_MGR_VIEW_FEEDBACK;}
            else if(choice==5){req.opcode=REQ_MGR_LIST_EMPLOYEES;}
            else if(choice==6){ChangePasswordPayload p;memset(&p,0,sizeof(p));printf("Old password: ");scanf("%63s",p.old_password);printf("New password: ");scanf("%63s",p.new_password);clear_input_buffer();req.opcode=REQ_CHANGE_PASSWORD;memcpy(req.payload,&p,sizeof(p));}
            else {puts("Invalid choice.");continue;}
        } else {
            if(choice==1){CreateCustomerPayload p;memset(&p,0,sizeof(p));printf("Employee username: ");scanf("%63s",p.username);printf("Temporary password: ");scanf("%63s",p.password);clear_input_buffer();req.opcode=REQ_ADM_ADD_EMPLOYEE;memcpy(req.payload,&p,sizeof(p));}
            else if(choice==2){ModifyUserPayload p;memset(&p,0,sizeof(p));printf("User ID: ");scanf("%d",&p.user_id);clear_input_buffer();printf("New username (blank to keep current): ");fgets(p.username,sizeof(p.username),stdin);p.username[strcspn(p.username,"\n")]=0;printf("New customer name (only for customer, blank to keep): ");fgets(p.customer_name,sizeof(p.customer_name),stdin);p.customer_name[strcspn(p.customer_name,"\n")]=0;req.opcode=REQ_ADM_MODIFY_DETAILS;memcpy(req.payload,&p,sizeof(p));}
            else if(choice==3){ManageRolePayload p;memset(&p,0,sizeof(p));printf("User ID: ");scanf("%d",&p.user_id);printf("New role (1 customer, 2 employee, 3 manager, 4 admin): ");scanf("%d",&p.role);clear_input_buffer();req.opcode=REQ_ADM_MANAGE_ROLE;memcpy(req.payload,&p,sizeof(p));}
            else if(choice==4){req.opcode=REQ_ADM_LIST_USERS;}
            else if(choice==5){ChangePasswordPayload p;memset(&p,0,sizeof(p));printf("Old password: ");scanf("%63s",p.old_password);printf("New password: ");scanf("%63s",p.new_password);clear_input_buffer();req.opcode=REQ_CHANGE_PASSWORD;memcpy(req.payload,&p,sizeof(p));}
            else {puts("Invalid choice.");continue;}
        }
        if(exchange(sock,&req,&res)<0){puts("Network error; returning to main menu.");break;}
        printf("\n[%d] %s\n",res.status_code,res.message);
        if(res.status_code!=RESP_OK) continue;
        if(req.opcode==REQ_EMP_VIEW_ASSIGNED_LOANS||req.opcode==REQ_MGR_VIEW_UNASSIGNED_LOANS){LoanListPayload p;memset(&p,0,sizeof(p));memcpy(&p,res.payload,sizeof(p));print_loan_list(&p);}
        else if(req.opcode==REQ_MGR_VIEW_FEEDBACK){FeedbackListPayload p;memset(&p,0,sizeof(p));memcpy(&p,res.payload,sizeof(p));for(int i=0;i<p.count&&i<6;i++)printf("Feedback %d | customer %s (%d): %s\n",p.feedback[i].feedback_id,p.feedback[i].customer_name,p.feedback[i].customer_id,p.feedback[i].message);}
        else if(req.opcode==REQ_MGR_LIST_EMPLOYEES){EmployeeListPayload p;memset(&p,0,sizeof(p));memcpy(&p,res.payload,sizeof(p));for(int i=0;i<p.count&&i<10;i++)printf("Employee ID %d | %s\n",p.employees[i].id,p.employees[i].username);}
        else if(req.opcode==REQ_ADM_LIST_USERS){UserListPayload p;memset(&p,0,sizeof(p));memcpy(&p,res.payload,sizeof(p));for(int i=0;i<p.count&&i<10;i++)printf("ID %d | %-20s | role %d | %s\n",p.users[i].id,p.users[i].username,p.users[i].role,p.users[i].is_active?"active":"inactive");}
        else if(req.opcode==REQ_EMP_VIEW_CUSTOMER_TXNS){CustomerTransactionsPayload p;memset(&p,0,sizeof(p));memcpy(&p,res.payload,sizeof(p));for(int i=0;i<p.count&&i<MAX_PASSBOOK_ENTRIES;i++)printf("Txn %d | type %d | amount %.2f | balance %.2f | %s\n",p.records[i].transaction_id,p.records[i].type,p.records[i].amount,p.records[i].balance_after,p.records[i].remarks);}
    }
}

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    printf("=====================================================\n");
    printf("   Banking Management System - Client Terminal       \n");
    printf("=====================================================\n");
    printf("[*] Connecting to Banking Server at %s:%d...\n", host, port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed. Is the banking server running?");
        close(sock);
        return 1;
    }

    printf("[+] Successfully connected to Banking Server!\n\n");

    int choice = 0;
    while (1) {
        printf("=====================================================\n");
        printf("            MAIN MENU - CLIENT TERMINAL              \n");
        printf("=====================================================\n");
        printf(" 1. User Login (Customer, Employee, Manager, Admin) \n");
        printf(" 2. View Pre-seeded Test Accounts                   \n");
        printf(" 3. Disconnect & Exit                               \n");
        printf("=====================================================\n");
        printf("Enter choice (1-3): ");

        if (scanf("%d", &choice) != 1) {
            clear_input_buffer();
            break;
        }
        clear_input_buffer();

        if (choice == 1) {
            LoginPayload login;
            memset(&login, 0, sizeof(login));

            printf("\n--- User Login ---\n");
            printf("Enter Username: ");
            if (scanf("%63s", login.username) != 1) { clear_input_buffer(); continue; }
            printf("Enter Password: ");
            if (scanf("%63s", login.password) != 1) { clear_input_buffer(); continue; }
            clear_input_buffer();

            RequestPacket req;
            ResponsePacket res;
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_LOGIN;
            memcpy(req.payload, &login, sizeof(login));

            printf("\n[*] Sending login request to server...\n");
            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("[-] Error communicating with server.\n");
                break;
            }

            if (res.status_code == RESP_OK) {
                AuthSuccessPayload auth;
                memcpy(&auth, res.payload, sizeof(auth));
                printf("[+] %s\n", res.message);

                if (auth.role == ROLE_CUSTOMER) {
                    handle_customer_session(sock, &auth);
                } else {
                    handle_staff_session(sock, &auth);
                }
            } else {
                printf("\n[-] Login Failed [%d]: %s\n\n", res.status_code, res.message);
            }

        } else if (choice == 2) {
            show_about_accounts();
        } else if (choice == 3) {
            RequestPacket req;
            ResponsePacket res;
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_EXIT;
            send_request(sock, &req);
            recv_response(sock, &res);
            printf("\n%s\n", res.message);
            break;
        } else {
            printf("\n[-] Invalid choice. Please enter 1-3.\n");
        }
    }

    close(sock);
    printf("[*] Client closed connection. Goodbye!\n");
    return 0;
}
