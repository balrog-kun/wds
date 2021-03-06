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
#include <gio/gio.h>
#include <memory>

#include "connman-client.h"

namespace P2P {

void ConnmanClient::connman_appeared_cb(GDBusConnection *connection, const char *name, const char *owner, gpointer data_ptr)
{
    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              "net.connman",
                              "/",
                              "net.connman.Manager",
                              NULL,
                              ConnmanClient::proxy_cb,
                              data_ptr);

    /* TODO should get the p2p object path
     * by watching Manager.TechnologyAdded/TechnologyRemoved */
    g_dbus_proxy_new_for_bus (G_BUS_TYPE_SYSTEM,
                              G_DBUS_PROXY_FLAGS_NONE,
                              NULL,
                              "net.connman",
                              "/net/connman/technology/p2p",
                              "net.connman.Technology",
                              NULL,
                              ConnmanClient::technology_proxy_cb,
                              data_ptr);
}

void ConnmanClient::connman_disappeared_cb(GDBusConnection *connection, const char *name, gpointer data_ptr)
{
    auto client = static_cast<ConnmanClient*> (data_ptr);
    client->connman_disappeared ();
}

/* static C callback */
void ConnmanClient::proxy_signal_cb (GDBusProxy *proxy, const char *sender, const char *signal, GVariant *params, gpointer data_ptr)
{
    if (g_strcmp0(signal, "PeersChanged") != 0)
        return;

    auto client = static_cast<ConnmanClient*> (data_ptr);
    client->peers_changed (params);
}

void ConnmanClient::get_technologies_cb (GObject *object, GAsyncResult *res, gpointer data_ptr)
{
    GError *error = NULL;
    GDBusProxy *proxy = G_DBUS_PROXY (object);
    char *object_path;
    bool p2p_found = false;
    GVariant *params;
    GVariantIter *iter;

    params = g_dbus_proxy_call_finish (proxy, res, &error);
    if (error) {
        std::cout << "GetTechnologies error " << error->message << std::endl;
        g_clear_error (&error);
    }

    g_variant_get (params, "(a(oa{sv}))", &iter);
    while (g_variant_iter_loop (iter, "(oa{sv})", &object_path, NULL)) {
        /* TODO: warn if P2P is not enabled. Also, don't set
         * the client available before P2P is enabled */
        if (g_strcmp0(object_path, "/net/connman/technology/p2p") == 0) {
            p2p_found = true;
            break;
        }
    }
    g_variant_unref(params);
    g_variant_iter_free (iter);

    if (!p2p_found)
        std::cout << "Warning: P2P not found in Connman technologies." << std::endl;
}

void ConnmanClient::register_peer_service_cb (GObject *object, GAsyncResult *res, gpointer data_ptr)
{
    GError *error = NULL;
    GDBusProxy *proxy = G_DBUS_PROXY (object);

    g_dbus_proxy_call_finish (proxy, res, &error);
    if (error) {
        std::cout << "register error " << error->message << std::endl;
        g_clear_error (&error);
        return;
    }
}

void ConnmanClient::get_peers_cb (GObject *object, GAsyncResult *res, gpointer data_ptr)
{
    GError *error = NULL;
    GDBusProxy *proxy = G_DBUS_PROXY(object);
    GVariantIter *peer_iter;
    GVariant *params;

    params = g_dbus_proxy_call_finish(proxy, res, &error);
    if (error) {
        std::cout << "GetPeers error " << error->message << std::endl;
        g_clear_error(&error);
        return;
    }

    g_variant_get(params, "(a(oa{sv}))", &peer_iter);
    auto client = static_cast<ConnmanClient*>(data_ptr);
    client->handle_new_peers(peer_iter);

    g_variant_unref(params);
    g_variant_iter_free(peer_iter);
}

/* static C callback */
void ConnmanClient::scan_cb (GObject *object, GAsyncResult *res, gpointer data_ptr)
{
    GError *error = NULL;
    GDBusProxy *proxy = G_DBUS_PROXY (object);

    g_dbus_proxy_call_finish (proxy, res, &error);
    if (error) {
        std::cout << "scan error " << error->message << std::endl;
        g_clear_error (&error);
        return;
    }

    std::cout << "* scan complete"<< std::endl;
}

/* static C callback */
void ConnmanClient::proxy_cb (GObject *object, GAsyncResult *res, gpointer data_ptr)
{
    auto client = static_cast<ConnmanClient*> (data_ptr);
    client->proxy_cb (res);
}

/* static C callback */
void ConnmanClient::technology_proxy_cb (GObject *object, GAsyncResult *res, gpointer data_ptr)
{
    auto client = static_cast<ConnmanClient*> (data_ptr);
    client->technology_proxy_cb (res);
}


void ConnmanClient::connman_disappeared()
{
    bool was_available = is_available();

    if (proxy_)
        g_clear_object (&proxy_);
    if (technology_proxy_)
        g_clear_object (&technology_proxy_);

    if (observer_ && was_available)
        observer_->on_availability_changed(this);
}

void ConnmanClient::handle_new_peers (GVariantIter *added)
{
    GVariantIter *props;
    const char *path;

    while (g_variant_iter_loop(added, "(oa{sv})", &path, &props)) {
        try {
            peers_[path] = std::make_shared<P2P::ConnmanPeer>(path, props);
            if (observer_)
                observer_->on_peer_added(this, peers_[path]);
        } catch (std::invalid_argument &x) {
            /* Not a miracast peer */
        }
    }
}

void ConnmanClient::peers_changed (GVariant *params)
{
    GVariantIter *added, *removed;
    const char *path;

    g_variant_get(params, "(a(oa{sv})ao)", &added, &removed);

    handle_new_peers(added);

    while (g_variant_iter_loop (removed, "o", &path)) {
        auto it = peers_.find(path);
        if (it == peers_.end())
            return;

        if (observer_)
            observer_->on_peer_removed(this, it->second);

        peers_.erase(it);
    }

    g_variant_iter_free (added);
    g_variant_iter_free (removed);
}

void ConnmanClient::register_peer_service ()
{
    GVariantBuilder builder;

    /* HACK: Connman should figure out the "master" boolean on its own but it does not.
     * We need to do it here with InformationElement for now... */
    P2P::InformationElement ie(array_);
    bool is_master = (ie.get_device_type() != P2P::SOURCE);

    g_variant_builder_init (&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add (&builder, "{sv}",
                           "WiFiDisplayIEs",
                           g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                                      array_->bytes,
                                                      array_->length,
                                                      1));

    g_dbus_proxy_call (proxy_,
                       "RegisterPeerService",
                       g_variant_new ("(a{sv}b)", &builder, is_master),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       ConnmanClient::register_peer_service_cb,
                       this);
}

void ConnmanClient::unregister_peer_service ()
{
    GVariantBuilder builder;
    g_variant_builder_init (&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add (&builder, "{sv}",
                           "WiFiDisplayIEs",
                           g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                                      array_->bytes,
                                                      array_->length,
                                                      1));

    g_dbus_proxy_call (proxy_,
                       "UnregisterPeerService",
                       g_variant_new ("(a{sv}b)", &builder, TRUE),
                       G_DBUS_CALL_FLAGS_NONE,
                       -1,
                       NULL,
                       ConnmanClient::register_peer_service_cb,
                       this);
}

void ConnmanClient::initialize_peers ()
{
    g_dbus_proxy_call(proxy_,
                      "GetPeers",
                      NULL,
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      NULL,
                      ConnmanClient::get_peers_cb,
                      this);
}


void ConnmanClient::proxy_cb (GAsyncResult *result)
{
    GError *error = NULL;

    proxy_ = g_dbus_proxy_new_for_bus_finish(result, &error);
    if (error) {
        std::cout << "proxy error "<< std::endl;
        g_clear_error (&error);
        return;
    }

    g_signal_connect(proxy_, "g-signal",
                     G_CALLBACK (ConnmanClient::proxy_signal_cb), this);

    g_dbus_proxy_call(proxy_,
                      "GetTechnologies",
                      NULL,
                      G_DBUS_CALL_FLAGS_NONE,
                      -1,
                      NULL,
                      ConnmanClient::get_technologies_cb,
                      this);

    initialize_peers();
    register_peer_service();

    if(observer_ && is_available())
        observer_->on_availability_changed(this);
}

void ConnmanClient::technology_proxy_cb (GAsyncResult *result)
{
    GError *error = NULL;

    technology_proxy_ = g_dbus_proxy_new_for_bus_finish(result, &error);
    if (error) {
        std::cout << "tech proxy error "<< std::endl;
        g_clear_error (&error);
    }

    if(observer_ && is_available())
        observer_->on_availability_changed(this);
}

ConnmanClient::ConnmanClient(const Parameters &params, Observer *observer):
    Client(observer),
    proxy_(NULL),
    technology_proxy_(NULL)
{
    set_ie_array_from_parameters(params);
    connman_watcher_ = g_bus_watch_name (G_BUS_TYPE_SYSTEM,
                                         "net.connman",
                                         G_BUS_NAME_WATCHER_FLAGS_NONE,
                                         ConnmanClient::connman_appeared_cb,
                                         ConnmanClient::connman_disappeared_cb,
                                         this, NULL);
}

ConnmanClient::~ConnmanClient()
{
    if (connman_watcher_ != 0) {
        g_bus_unwatch_name (connman_watcher_);
        connman_watcher_ = 0;
    }
    if (proxy_)
        g_clear_object (&proxy_);
    if (technology_proxy_)
        g_clear_object (&technology_proxy_);
}

void ConnmanClient::set_parameters(const Parameters &params)
{
    g_return_if_fail (is_available());

    unregister_peer_service();
    set_ie_array_from_parameters(params);
    register_peer_service();
}

bool ConnmanClient::is_available() const
{
    return proxy_ && technology_proxy_;
}

void ConnmanClient::scan()
{
    g_return_if_fail (is_available());

    g_dbus_proxy_call (technology_proxy_,
                       "Scan",
                       NULL,
                       G_DBUS_CALL_FLAGS_NONE,
                       60 * 1000,
                       NULL,
                       ConnmanClient::scan_cb,
                       this);
}

void ConnmanClient::set_ie_array_from_parameters(const Parameters &params)
{
    P2P::InformationElement ie;
    auto sub_element = P2P::new_subelement(P2P::DEVICE_INFORMATION);
    auto dev_info = (P2P::DeviceInformationSubelement *) sub_element;

    if (params.source && params.session_management_control_port != 0)
        dev_info->session_management_control_port =
            g_htons(params.session_management_control_port);
    else
        dev_info->session_management_control_port = g_htons(7236);

    dev_info->maximum_throughput = g_htons(50);

    if (params.source) {
        if (params.sink)
            dev_info->field1.device_type = P2P::DUAL_ROLE;
	else
            dev_info->field1.device_type = P2P::SOURCE;
    } else
        dev_info->field1.device_type = P2P::PRIMARY_SINK;

    dev_info->field1.session_availability = true;
    ie.add_subelement(sub_element);
    array_ = ie.serialize();
}

}
