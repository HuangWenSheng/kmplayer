/**
 * Copyright (C) 2002-2003 by Koos Vriezen <koos ! vriezen ? xs4all ! nl>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License version 2 as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA 02111-1307, USA.
 **/

#ifndef KMPLAYER_PART_H
#define KMPLAYER_PART_H

#include <kparts/browserextension.h>
#include "kmplayerpartbase.h"
#include "kmplayersource.h"


class KAboutData;
class KMPlayerPart;
class KInstance;
class JSCommandEntry;


class KMPlayerHRefSource : public KMPlayerSource {
    Q_OBJECT
public:
    KMPlayerHRefSource (KMPlayer * player);
    virtual ~KMPlayerHRefSource ();
    virtual bool processOutput (const QString & line);
    virtual bool hasLength ();

    void setURL (const KURL &);
    void clear ();
    virtual QString prettyName ();
public slots:
    virtual void init ();
    virtual void activate ();
    virtual void deactivate ();
private slots:
    void grabReady (const QString & path);
    void play ();
    void finished ();
private:
    QString m_grabfile;
    bool m_finished;
};


class KMPlayerBrowserExtension : public KParts::BrowserExtension {
    Q_OBJECT
public:
    KMPlayerBrowserExtension(KMPlayerPart *parent);
    void urlChanged (const QString & url);
    void setLoadingProgress (int percentage);

    void setURLArgs (const KParts::URLArgs & args);
    void saveState (QDataStream & stream);
    void restoreState (QDataStream & stream);
};

class KMPlayerLiveConnectExtension : public KParts::LiveConnectExtension {
    Q_OBJECT
public:
    KMPlayerLiveConnectExtension (KMPlayerPart * parent);
    ~KMPlayerLiveConnectExtension ();

    // LiveConnect interface
    bool get (const unsigned long, const QString &,
            KParts::LiveConnectExtension::Type &, unsigned long &, QString &);
    bool put (const unsigned long, const QString &, const QString &);
    bool call (const unsigned long, const QString &,
            const QStringList &, KParts::LiveConnectExtension::Type &, 
            unsigned long &, QString &);
    void unregister (const unsigned long);
    void sendEvent(const unsigned long objid, const QString & event, const KParts::LiveConnectExtension::ArgList & args ) {
        emit partEvent(objid, event, args);
    }

    void enableFinishEvent (bool b = true) { m_enablefinish = b; }
signals:
    void partEvent (const unsigned long, const QString &,
                    const KParts::LiveConnectExtension::ArgList &);
public slots:
    void setSize (int w, int h);
private slots:
    void started ();
    void finished ();
private:
    KMPlayerPart * player;
    const JSCommandEntry * lastJSCommandEntry;
    bool m_started : 1;
    bool m_enablefinish : 1;
};


class KMPlayerPart : public KMPlayer {
    Q_OBJECT
public:
    enum Features {
        Feat_Viewer = 0x01, Feat_Controls = 0x02, Feat_Label = 0x04,
        Feat_All = 0xff
    };
    KMPlayerPart (QWidget * wparent, const char * wname,
              QObject * parent, const char * name, const QStringList &args);
    ~KMPlayerPart ();

    KMPlayerBrowserExtension * browserextension() const
        { return m_browserextension; }
    KMPlayerLiveConnectExtension * liveconnectextension () const
        { return m_liveconnectextension; }
    KMPlayerHRefSource * hrefSource () const { return m_hrefsource; }
    bool hasFeature (int f) { return m_features & f; }
    bool allowRedir (const QString & url);
public slots:
    virtual bool openURL (const KURL & url);
    virtual bool closeURL ();
    void setMenuZoom (int id);
protected slots:
    virtual void processStarted ();
    virtual void processFinished ();
    virtual void processLoading (int percentage);
    virtual void processPlaying ();
protected:
    KMPlayerBrowserExtension * m_browserextension;
    KMPlayerLiveConnectExtension * m_liveconnectextension;
    KMPlayerHRefSource * m_hrefsource;
    QString m_group;
    QString m_docbase;
    int m_features;
    bool m_started_emited : 1;
    //bool m_noresize : 1;
    bool m_havehref : 1;
};


#endif
