/* This file is part of the KDE project
 *
 * Copyright (C) 2006 Koos Vriezen <koos.vriezen@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _KMPLAYER_XSPF_H_
#define _KMPLAYER_XSPF_H_

#include <qstring.h>

#include "kmplayerplaylist.h"

namespace KMPlayer {

namespace XSPF {

const short id_node_playlist = 500;
const short id_node_title = 501;
const short id_node_creator = 502;
const short id_node_annotation = 503;
const short id_node_info = 504;
const short id_node_location = 505;
const short id_node_identifier = 506;
const short id_node_image = 507;
const short id_node_date = 508;
const short id_node_license = 509;
const short id_node_attribution = 510;
const short id_node_meta = 511;
const short id_node_extension = 512;
const short id_node_tracklist = 513;
const short id_node_track = 514;
const short id_node_album = 515;
const short id_node_tracknum = 516;
const short id_node_duration = 517;
const short id_node_link = 518;

class Playlist : public Mrl
{
public:
    Playlist (NodePtr & d) : Mrl (d, id_node_playlist) {}
    Node *childFromTag (const QString & tag) override;
    const char * nodeName () const override { return "playlist"; }
    void *role (RoleType msg, void *content=nullptr) override;
    void closed () override;
};

class Tracklist : public Element
{
public:
    Tracklist (NodePtr & d) : Element (d, id_node_tracklist) {}
    Node *childFromTag (const QString & tag) override;
    const char * nodeName () const override { return "tracklist"; }
};

class Track : public Element
{
public:
    Track (NodePtr & d) : Element (d, id_node_track) {}
    void closed () override;
    void activate () override;
    const char * nodeName () const override { return "track"; }
    Node *childFromTag (const QString & tag) override;
};

class Location : public Mrl
{
public:
    Location (NodePtr &d) : Mrl (d, id_node_location) {}
    const char * nodeName () const override { return "location"; }
    void closed () override;
};

} //namespace XSPF


} // namespace KMPlayer

#endif //_KMPLAYER_XSPF_H_
