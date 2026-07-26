# Copyright (c) 2024-present The Kvanta5 Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include(GNUInstallDirs)

# TODO: libkvanta5kernel is a work in progress consensus engine
#       library, as more and more modules are decoupled from the
#       consensus engine, this list will shrink to only those
#       which are absolutely necessary.
add_library(kvanta5kernel
  kvanta5kernel.cpp
  chain.cpp
  checks.cpp
  chainparams.cpp
  coinstats.cpp
  context.cpp
  cs_main.cpp
  disconnected_transactions.cpp
  mempool_removal_reason.cpp
  ../arith_uint256.cpp
  ../chain.cpp
  ../coins.cpp
  ../compressor.cpp
  ../consensus/merkle.cpp
  ../consensus/tx_check.cpp
  ../consensus/tx_verify.cpp
  ../core_read.cpp
  ../dbwrapper.cpp
  ../deploymentinfo.cpp
  ../deploymentstatus.cpp
  ../flatfile.cpp
  ../hash.cpp
  ../logging.cpp
  ../node/blockstorage.cpp
  ../node/chainstate.cpp
  ../node/utxo_snapshot.cpp
  ../policy/ephemeral_policy.cpp
  ../policy/feerate.cpp
  ../policy/packages.cpp
  ../policy/policy.cpp
  ../policy/rbf.cpp
  ../policy/settings.cpp
  ../policy/truc_policy.cpp
  ../pow.cpp
  ../primitives/block.cpp
  ../primitives/transaction.cpp
  ../pubkey.cpp
  ../random.cpp
  ../randomenv.cpp
  ../script/interpreter.cpp
  ../script/script.cpp
  ../script/script_error.cpp
  ../script/sigcache.cpp
  ../script/solver.cpp
  ../script/kvanta5_p2qr.cpp
  ../signet.cpp
  ../streams.cpp
  ../support/lockedpool.cpp
  ../sync.cpp
  ../txdb.cpp
  ../txmempool.cpp
  ../uint256.cpp
  ../util/chaintype.cpp
  ../util/check.cpp
  ../util/feefrac.cpp
  ../util/fs.cpp
  ../util/fs_helpers.cpp
  ../util/hasher.cpp
  ../util/moneystr.cpp
  ../util/rbf.cpp
  ../util/serfloat.cpp
  ../util/signalinterrupt.cpp
  ../util/strencodings.cpp
  ../util/string.cpp
  ../util/syserror.cpp
  ../util/threadnames.cpp
  ../util/time.cpp
  ../util/tokenpipe.cpp
  ../validation.cpp
  ../validationinterface.cpp
  ../versionbits.cpp
)
target_link_libraries(kvanta5kernel
  PRIVATE
    core_interface
    kvanta5_clientversion
    kvanta5_crypto
    kvanta5_rocksdb
    secp256k1
    $<TARGET_NAME_IF_EXISTS:USDT::headers>
  PUBLIC
    Boost::headers
)

# libkvanta5kernel requires default symbol visibility, explicitly
# specify that here so that things still work even when user
# configures with -DREDUCE_EXPORTS=ON
#
# Note this is a quick hack that will be removed as we
# incrementally define what to export from the library.
set_target_properties(kvanta5kernel PROPERTIES
  CXX_VISIBILITY_PRESET default
)

# Add a convenience libkvanta5kernel target as a synonym for kvanta5kernel.
add_custom_target(libkvanta5kernel)
add_dependencies(libkvanta5kernel kvanta5kernel)

# When building the static library, install all static libraries the
# kvanta5kernel depends on.
if(NOT BUILD_SHARED_LIBS)
  # Recursively get all the static libraries a target depends on and put them in libs_out
  function(get_target_static_link_libs target libs_out)
    get_target_property(linked_libraries ${target} LINK_LIBRARIES)
    foreach(dep ${linked_libraries})
      if(TARGET ${dep})
        add_dependencies(libkvanta5kernel ${dep})
        get_target_property(dep_type ${dep} TYPE)
        if(dep_type STREQUAL "STATIC_LIBRARY")
          list(APPEND ${libs_out} ${dep})
          get_target_static_link_libs(${dep} ${libs_out})
        endif()
      endif()
    endforeach()
    set(${libs_out} ${${libs_out}} PARENT_SCOPE)
  endfunction()

  set(all_kernel_static_link_libs "")
  get_target_static_link_libs(kvanta5kernel all_kernel_static_link_libs)

  install(TARGETS ${all_kernel_static_link_libs} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT libkvanta5kernel)
  list(TRANSFORM all_kernel_static_link_libs PREPEND "-l")
  # LIBS_PRIVATE is substituted in the pkg-config file.
  list(JOIN all_kernel_static_link_libs " " LIBS_PRIVATE)
endif()

configure_file(${PROJECT_SOURCE_DIR}/libkvanta5kernel.pc.in ${PROJECT_BINARY_DIR}/libkvanta5kernel.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/libkvanta5kernel.pc DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig" COMPONENT libkvanta5kernel)

install(TARGETS kvanta5kernel
  RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT libkvanta5kernel
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT libkvanta5kernel
  ARCHIVE
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT libkvanta5kernel
)
