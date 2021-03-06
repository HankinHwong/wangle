/*
 *  Copyright (c) 2015, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include <folly/io/async/EventBase.h>
#include <folly/io/async/SSLContext.h>

#include <glog/logging.h>
#include <list>
#include <memory>
#include <wangle/ssl/SSLContextConfig.h>
#include <wangle/ssl/SSLSessionCacheManager.h>
#include <wangle/ssl/TLSTicketKeySeeds.h>
#include <wangle/acceptor/SSLContextSelectionMisc.h>
#include <vector>

namespace folly {

class SocketAddress;
class SSLContext;

}

namespace wangle {

class ClientHelloExtStats;
struct SSLCacheOptions;
class SSLStats;
class TLSTicketKeyManager;
struct TLSTicketKeySeeds;

class SSLContextManager {
 public:

  explicit SSLContextManager(folly::EventBase* eventBase,
                             const std::string& vipName, bool strict,
                             SSLStats* stats);
  virtual ~SSLContextManager();

  /**
   * Add a new X509 to SSLContextManager.  The details of a X509
   * is passed as a SSLContextConfig object.
   *
   * @param ctxConfig     Details of a X509, its private key, password, etc.
   * @param cacheOptions  Options for how to do session caching.
   * @param ticketSeeds   If non-null, the initial ticket key seeds to use.
   * @param vipAddress    Which VIP are the X509(s) used for? It is only for
   *                      for user friendly log message
   * @param externalCache Optional external provider for the session cache;
   *                      may be null
   */
  void addSSLContextConfig(
    const SSLContextConfig& ctxConfig,
    const SSLCacheOptions& cacheOptions,
    const TLSTicketKeySeeds* ticketSeeds,
    const folly::SocketAddress& vipAddress,
    const std::shared_ptr<SSLCacheProvider> &externalCache);

  /**
   * Get the default SSL_CTX for a VIP
   */
  std::shared_ptr<folly::SSLContext>
    getDefaultSSLCtx() const;

  /**
   * Search first by exact domain, then by one level up
   */
  std::shared_ptr<folly::SSLContext>
    getSSLCtx(const SSLContextKey& key) const;

  /**
   * Search by the _one_ level up subdomain
   */
  std::shared_ptr<folly::SSLContext>
    getSSLCtxBySuffix(const SSLContextKey& key) const;

  /**
   * Search by the full-string domain name
   */
  std::shared_ptr<folly::SSLContext>
    getSSLCtxByExactDomain(const SSLContextKey& key) const;

  /**
   * Insert a SSLContext by domain name.
   */
  void insertSSLCtxByDomainName(
    const char* dn,
    size_t len,
    std::shared_ptr<folly::SSLContext> sslCtx,
    CertCrypto certCrypto = CertCrypto::BEST_AVAILABLE);

  void reloadTLSTicketKeys(const std::vector<std::string>& oldSeeds,
                           const std::vector<std::string>& currentSeeds,
                           const std::vector<std::string>& newSeeds);

  void setSSLStats(SSLStats* stats) {
    stats_ = stats;
  }

  /**
   * SSLContextManager only collects SNI stats now
   */
  void setClientHelloExtStats(ClientHelloExtStats* stats) {
    clientHelloTLSExtStats_ = stats;
  }

 protected:
  virtual void enableAsyncCrypto(
    const std::shared_ptr<folly::SSLContext>& sslCtx) {
    LOG(FATAL) << "Unsupported in base SSLContextManager";
  }
  SSLStats* stats_{nullptr};

 private:
  SSLContextManager(const SSLContextManager&) = delete;

  void ctxSetupByOpensslFeature(
    std::shared_ptr<folly::SSLContext> sslCtx,
    const SSLContextConfig& ctxConfig);

  /**
   * Callback function from openssl to find the right X509 to
   * use during SSL handshake
   */
#if OPENSSL_VERSION_NUMBER >= 0x1000105fL && \
    !defined(OPENSSL_NO_TLSEXT) && \
    defined(SSL_CTRL_SET_TLSEXT_SERVERNAME_CB)
# define PROXYGEN_HAVE_SERVERNAMECALLBACK
  folly::SSLContext::ServerNameCallbackResult
    serverNameCallback(SSL* ssl);
#endif

  /**
   * The following functions help to maintain the data structure for
   * domain name matching in SNI.  Some notes:
   *
   * 1. It is a best match.
   *
   * 2. It allows wildcard CN and wildcard subject alternative name in a X509.
   *    The wildcard name must be _prefixed_ by '*.'.  It errors out whenever
   *    it sees '*' in any other locations.
   *
   * 3. It uses one std::unordered_map<DomainName, SSL_CTX> object to
   *    do this.  For wildcard name like "*.facebook.com", ".facebook.com"
   *    is used as the key.
   *
   * 4. After getting tlsext_hostname from the client hello message, it
   *    will do a full string search first and then try one level up to
   *    match any wildcard name (if any) in the X509.
   *    [Note, browser also only looks one level up when matching the requesting
   *     domain name with the wildcard name in the server X509].
   */

  void insert(
    std::shared_ptr<folly::SSLContext> sslCtx,
    std::unique_ptr<SSLSessionCacheManager> cmanager,
    std::unique_ptr<TLSTicketKeyManager> tManager,
    bool defaultFallback);

  void insertSSLCtxByDomainNameImpl(
    const char* dn,
    size_t len,
    std::shared_ptr<folly::SSLContext> sslCtx,
    CertCrypto certCrypto);

  void insertIntoDnMap(SSLContextKey key,
    std::shared_ptr<folly::SSLContext> sslCtx,
    bool overwrite);


  /**
   * Container to own the SSLContext, SSLSessionCacheManager and
   * TLSTicketKeyManager.
   */
  std::vector<std::shared_ptr<folly::SSLContext>> ctxs_;
  std::vector<std::unique_ptr<SSLSessionCacheManager>>
    sessionCacheManagers_;
  std::vector<std::unique_ptr<TLSTicketKeyManager>> ticketManagers_;

  std::shared_ptr<folly::SSLContext> defaultCtx_;
  std::string defaultCtxDomainName_;

  /**
   * Container to store the (DomainName -> SSL_CTX) mapping
   */
  std::unordered_map<
    SSLContextKey,
    std::shared_ptr<folly::SSLContext>,
    SSLContextKeyHash> dnMap_;

  folly::EventBase* eventBase_;
  ClientHelloExtStats* clientHelloTLSExtStats_{nullptr};
  SSLContextConfig::SNINoMatchFn noMatchFn_;
  bool strict_{true};
};

} // namespace wangle
