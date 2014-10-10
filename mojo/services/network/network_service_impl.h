// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_
#define MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_

#include "base/compiler_specific.h"
#include "mojo/public/cpp/bindings/interface_impl.h"
#include "mojo/services/public/interfaces/network/network_service.mojom.h"
#include "url/gurl.h"

namespace mojo {
class ApplicationConnection;
class NetworkContext;

class NetworkServiceImpl : public InterfaceImpl<NetworkService> {
 public:
  NetworkServiceImpl(ApplicationConnection* connection,
                     NetworkContext* context);
  virtual ~NetworkServiceImpl();

  // NetworkService methods:
  virtual void CreateURLLoader(InterfaceRequest<URLLoader> loader) override;
  virtual void GetCookieStore(InterfaceRequest<CookieStore> store) override;
  virtual void CreateWebSocket(InterfaceRequest<WebSocket> socket) override;
  virtual void CreateTCPBoundSocket(
      NetAddressPtr local_address,
      InterfaceRequest<TCPBoundSocket> bound_socket,
      const Callback<void(NetworkErrorPtr, NetAddressPtr)>& callback) override;
  virtual void CreateTCPConnectedSocket(
      NetAddressPtr remote_address,
      ScopedDataPipeConsumerHandle send_stream,
      ScopedDataPipeProducerHandle receive_stream,
      InterfaceRequest<TCPConnectedSocket> client_socket,
      const Callback<void(NetworkErrorPtr, NetAddressPtr)>& callback) override;
  virtual void CreateUDPSocket(InterfaceRequest<UDPSocket> socket) override;

 private:
  NetworkContext* context_;
  GURL origin_;
};

}  // namespace mojo

#endif  // MOJO_SERVICES_NETWORK_NETWORK_SERVICE_IMPL_H_
