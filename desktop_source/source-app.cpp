/*
 * This file is part of Wireless Display Software for Linux OS
 *
 * Copyright (C) 2014 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <iostream>

#include "source-app.h"
#include "multi-client.h"

void SourceApp::on_availability_changed(P2P::Client *client)
{
    if (client->is_available())
        scan();
}

void SourceApp::on_peer_added(P2P::Client *client, std::shared_ptr<P2P::Peer> peer)
{
    if (peer->device_type() == P2P::PRIMARY_SINK) {
        peer->set_observer(this);
    }
}

void SourceApp::on_peer_removed(P2P::Client *client, std::shared_ptr<P2P::Peer> peer)
{
    for (auto it = peers_.begin(); it != peers_.end();) {
        if (it->second == peer.get()) {
            peers_.erase (it++);
            return;
        } else {
            ++it;
        }
    }
}

void SourceApp::on_initialized(P2P::Peer *peer)
{
    peers_[peer_index_] = peer;
    std::cout << "  "<< peer_index_ << " : " << peer->name() << std::endl;
    peer_index_++;
}

void SourceApp::scan()
{
    std::cout << "* Now scanning for peers..." << std::endl;
    p2p_client_->scan();
}

bool SourceApp::connect(uint peer_index)
{
    auto it = peers_.find (peer_index);
    if (it == peers_.end()) {
        std::cout << "No such peer" << std::endl;
        return false;
    }

    it->second->connect ();
    return true;
}

void SourceApp::on_availability_changed(P2P::Peer *peer)
{
    if (!peer->is_available())
        return;

    std::cout << "* Connected to " << peer->remote_host()  << std::endl;
}

SourceApp::SourceApp(int port) :
    peer_index_(0)
{
    static struct P2P::Parameters params = {
        .source = true,
        .session_management_control_port = (uint16_t) port,
    };

    // register the P2P service with the DBus service in use
    std::cout << "* Registering Wi-Fi Display Source" <<  std::endl;
    p2p_client_.reset(new P2P::MultiClient(params, this));

    source_.reset(new MiracBrokerSource(port));
}

SourceApp::~SourceApp()
{
}
