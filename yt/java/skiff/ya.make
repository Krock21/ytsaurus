JAVA_LIBRARY(ytsaurus-skiff)

IF(JDK_VERSION == "")
    JDK_VERSION(11)
ENDIF()

LINT(extended)

INCLUDE(${ARCADIA_ROOT}/yt/gradle.inc)

CHECK_JAVA_DEPS(yes)

PEERDIR(
    yt/java/annotations
    yt/java/yson-tree
    yt/java/ytsaurus-client-core
)

JAVA_SRCS(SRCDIR src/main/java **/*)

END()

RECURSE_FOR_TESTS(
    src/test
)
