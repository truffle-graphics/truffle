add_library(truffle_base STATIC base.cpp)
truffle_configure_target(truffle_base Base)
add_library(Truffle::Base ALIAS truffle_base)
