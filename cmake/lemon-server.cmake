# ==================================================================================
# Lemon Online Server (embedded HTTP submission service)
# ==================================================================================

set(LEMON_BASEDIR_SERVER ${CMAKE_SOURCE_DIR}/src/server)

set(LEMON_SERVER_SOURCES
    ${LEMON_BASEDIR_SERVER}/UserStore.h
    ${LEMON_BASEDIR_SERVER}/UserStore.cpp
    ${LEMON_BASEDIR_SERVER}/SessionManager.h
    ${LEMON_BASEDIR_SERVER}/SessionManager.cpp
    ${LEMON_BASEDIR_SERVER}/SubmissionServer.h
    ${LEMON_BASEDIR_SERVER}/SubmissionServer.cpp
    ${LEMON_BASEDIR_SERVER}/onlinepanel.h
    ${LEMON_BASEDIR_SERVER}/onlinepanel.cpp
)
