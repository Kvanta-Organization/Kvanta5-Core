# Copyright (c) 2023-present The Kvanta5 Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

function(generate_setup_nsi)
  set(abs_top_srcdir ${PROJECT_SOURCE_DIR})
  set(abs_top_builddir ${PROJECT_BINARY_DIR})
  set(CLIENT_URL ${PROJECT_HOMEPAGE_URL})
  set(CLIENT_TARNAME "kvanta5")
  set(KVANTA5_GUI_NAME "kvanta5-qt")
  set(KVANTA5_DAEMON_NAME "kvanta5d")
  set(KVANTA5_CLI_NAME "kvanta5-cli")
  set(KVANTA5_TX_NAME "kvanta5-tx")
  set(KVANTA5_WALLET_TOOL_NAME "kvanta5-wallet")
  set(KVANTA5_TEST_NAME "test_kvanta5")
  set(EXEEXT ${CMAKE_EXECUTABLE_SUFFIX})
  configure_file(${PROJECT_SOURCE_DIR}/share/setup.nsi.in ${PROJECT_BINARY_DIR}/kvanta5-win64-setup.nsi USE_SOURCE_PERMISSIONS @ONLY)
endfunction()
