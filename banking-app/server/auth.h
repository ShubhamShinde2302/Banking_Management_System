#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>
#include "common/models.h"
#include "common/protocol.h"

/* Authenticate a user login attempt */
int auth_login(const LoginPayload *login,
               AuthSuccessPayload *out_auth,
               int *session_fd_out,
               char *msg_out,
               size_t msg_len);

/* Log out an active user session */
int auth_logout(int user_id, int session_fd);

/* Change password for an authenticated user */
int auth_change_password(int user_id,
                         const ChangePasswordPayload *payload,
                         char *msg_out,
                         size_t msg_len);

/* Helper to convert UserRole enum to human-readable string */
const char *role_to_string(UserRole role);

#endif /* AUTH_H */
