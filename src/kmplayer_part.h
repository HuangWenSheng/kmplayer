/***************************************************************************
                          kmplayer_part.h  -  description
                             -------------------
    begin                : Sun Dec 8 2002
    copyright            : (C) 2002 by Koos Vriezen
    email                : 
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef KMPLAYER_PART_H
#define KMPLAYER_PART_H

#include <kmediaplayer/player.h>
#include <kparts/browserextension.h>
#include <kparts/factory.h>
#include <kurl.h>
#include <qobject.h>
#include <qstringlist.h>
#include <qguardedptr.h>
#include <qregexp.h>
#include "kmplayerview.h"


class KProcess;
class KAboutData;
class KMPlayer;
class KMPlayerConfig;
class KInstance;
class KConfig;

/**
  *@author Koos Vriezen
  */
class KMPlayerBrowserExtension : public KParts::BrowserExtension {
    Q_OBJECT
public:
    KMPlayerBrowserExtension(KMPlayer *parent);
    void urlChanged (const QString & url);
    void setLoadingProgress (int percentage);

    void setURLArgs (const KParts::URLArgs & args);
    void saveState (QDataStream & stream);
    void restoreState (QDataStream & stream);
};

class KMPlayerLiveConnectExtension : public KParts::LiveConnectExtension {
    Q_OBJECT
public:
    KMPlayerLiveConnectExtension (KMPlayer * parent);
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
signals:
    void partEvent (const unsigned long, const QString &,
                    const KParts::LiveConnectExtension::ArgList &);
public slots:
    void setSize (int w, int h);
private slots:
    void started ();
    void finished ();
private:
    KMPlayer * player;
    bool m_started;
};

class KMPlayer : public KMediaPlayer::Player  {
    Q_OBJECT
public: 
    KMPlayer (QWidget * parent, KConfig *);
    KMPlayer (QWidget * wparent, const char * wname,
              QObject * parent, const char * name, const QStringList &args);
    ~KMPlayer ();
    virtual KMediaPlayer::View* view ();
    static KAboutData* createAboutData ();

    KMPlayerConfig * configDialog () const { return m_configdialog; }
    int seekTime () const { return m_seektime; }
    void setSeekTime (int t) { m_seektime = t; }
    int cacheSize () const { return m_cachesize; }
    void setCacheSize (int s) { m_cachesize = s; }
    void keepMovieAspect (bool);
    KURL url () const { return m_url; }
    KMPlayerBrowserExtension * browserextension() const
        { return m_browserextension; }
    void setHRef (const QString & h) { m_href = h; }
    void sizes (int & w, int & h) const;
    void setMovieLength (int len);
public slots:
    virtual bool openURL (const KURL & url);
    virtual bool closeURL ();
    virtual void pause (void);
    virtual void play (void);
    virtual void stop (void);
    virtual void seek (unsigned long msec);
    virtual void seekPercent (float per);

    void adjustVolume (int incdec);
    void setURL (const KURL & url);
    bool run (const char * args, const char * pipe = 0L);
    bool playing () const;
    void showConfigDialog ();
    void setMenuZoom (int id);
    void posSliderChanged (int pos);
public:
    virtual bool isSeekable (void) const { return m_movie_length > 0; }
    virtual unsigned long position (void) const { return m_movie_position; }
    virtual bool hasLength (void) const { return m_movie_length > 0; }
    virtual unsigned long length (void) const { return m_movie_length; }
signals:
    void running ();
    void finished ();
protected:
    bool openFile();
    void timerEvent (QTimerEvent *);
private slots:
    void processOutput (KProcess *, char *, int);
    void processStopped (KProcess *);
    void processDataWritten (KProcess *);
    void back ();
    void forward ();
    void posSliderPressed ();
    void posSliderReleased ();
private:
    void init ();
    void initProcess ();
    void sendCommand (const QString &);
    KConfig * m_config;
    QGuardedPtr <KMPlayerView> m_view;
    KMPlayerConfig * m_configdialog;
    KProcess * m_process;
    KMPlayerBrowserExtension * m_browserextension;
    KMPlayerLiveConnectExtension * m_liveconnectextension;
    KURL m_url;
    QRegExp m_posRegExp;
    QStringList commands;
    QString m_href;
    QString m_process_output;
    int m_seektime;
    int m_cachesize;
    int movie_width;
    int movie_height;
    int m_movie_length;
    int m_movie_position;
    bool m_started_emited : 1;
    bool m_ispart : 1;
    bool m_use_slave : 1;
    bool m_bPosSliderPressed : 1;
};

class KMPlayerFactory : public KParts::Factory {
    Q_OBJECT
public:
    KMPlayerFactory ();
    virtual ~KMPlayerFactory ();
    virtual KParts::Part *createPartObject 
        (QWidget *wparent, const char *wname,
         QObject *parent, const char *name,
         const char *className, const QStringList &args);
    static KInstance * instance () { return s_instance; }
private:
    static KInstance * s_instance;
};

#endif
