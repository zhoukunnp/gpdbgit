/*
 * tdstore trans ctx factory 
 * kangsun@tencent.com
 */

#pragma once

#include "rocksdb/slice.h"
#include "common/td_atomic.h"
#include "td_component_factory.h"
#include "transactions/td_trans_log.h"

using namespace tdstore;
using namespace tdstore::common;

namespace rocksdb {
class TDPartTransCtx;

typedef TDComponentFactory<TDPartTransCtx> TDTransCtxFactory;
typedef TDComponentFactory<RlogBuf> RlogBufFactory;

typedef TDComponentMemory TDTransactionFactory;

template<class T>
T* transaction_alloc()
{
  T* ptr = NULL;
  TD_ALLOC(ptr, TDTransactionFactory, TDModIds::TD_TRANS_CTX, T);
  return ptr;
}

template<class T,typename Args >
T* transaction_alloc(Args args)
{
  T* ptr = NULL;
  TD_ARGS_ALLOC(ptr, TDTransactionFactory, TDModIds::TD_TRANS_CTX, T, args);
  return ptr;
}

template<class T>
void transaction_free(T* ptr)
{
  TD_FREE(ptr, TDTransactionFactory, T);
}

template <typename T>
class TDTransactionClassFactory
{
 public:
  static shared_ptr<T> create()
  {
      return shared_ptr<T> (
          transaction_alloc<T>(),
          transaction_free<T> );
  }
};

template <typename T,typename Args>
class TDTransactionClassArgsFactory
{
 public:
  static shared_ptr<T> create(Args args)
  {
      return shared_ptr<T> (
          transaction_alloc<T,Args> (args),
          transaction_free<T> );
  }
};

}
