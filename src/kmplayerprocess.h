/* This file is part of the KDE project
 *
 * Copyright (C) 2003 Koos Vriezen <koos.vriezen@xs4all.nl>
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

#ifndef _KMPLAYERPROCESS_H_
#define _KMPLAYERPROCESS_H_

#include <qobject.h>
#include <qguardedptr.h>
#include <qstring.h>
#include <qcstring.h>
#include <qstringlist.h>
#include <qregexp.h>

#include <kurl.h>

#include "kmplayerconfig.h"
#include "kmplayersource.h"

class QWidget;
class KProcess;

namespace KMPlayer {
    
class Settings;
class Viewer;
class Source;
class Callback;
class Backend_stub;

/*
 * Base class for all backend processes
 */
class KMPLAYER_EXPORT Process : public QObject {
    Q_OBJECT
public:
    enum State {
        NotRunning = 0, Ready, Buffering, Playing
    };
    Process (QObject * parent, Settings * settings, const char * n);
    virtual ~Process ();
    virtual void init ();
    virtual void initProcess (Viewer *);
    virtual QString menuName () const;
    bool playing () const;
    KDE_NO_EXPORT KProcess * process () const { return m_process; }
    KDE_NO_EXPORT Source * source () const { return m_source; }
    virtual WId widget ();
    Viewer * viewer () const;
    void setSource (Source * src) { m_source = src; }
    virtual bool grabPicture (const KURL & url, int pos);
    bool supports (const char * source) const;
    State state () const { return m_state; }
signals:
    void grabReady (const QString & path);
public slots:
    virtual bool ready (Viewer *);
    virtual bool play (Source *, NodePtr mrl);
    virtual bool stop ();
    virtual bool quit ();
    virtual bool pause ();
    /* seek (pos, abs) seek positon in deci-seconds */
    virtual bool seek (int pos, bool absolute);
    /* volume from 0 to 100 */
    virtual bool volume (int pos, bool absolute);
    /* saturation/hue/contrast/brightness from -100 to 100 */
    virtual bool saturation (int pos, bool absolute);
    virtual bool hue (int pos, bool absolute);
    virtual bool contrast (int pos, bool absolute);
    virtual bool brightness (int pos, bool absolute);
protected slots:
    void rescheduledStateChanged ();
protected:
    void setState (State newstate);
    QGuardedPtr <Viewer> m_viewer;
    Source * m_source;
    Settings * m_settings;
    NodePtrW m_mrl;
    State m_state;
    State m_old_state;
    KProcess * m_process;
    QString m_url;
    int m_request_seek;
    const char ** m_supported_sources;
};

/*
 * Base class for all MPlayer based processes
 */
class MPlayerBase : public Process {
    Q_OBJECT
public:
    MPlayerBase (QObject * parent, Settings * settings, const char * n);
    ~MPlayerBase ();
    void initProcess (Viewer *);
public slots:
    virtual bool stop ();
    virtual bool quit ();
protected:
    bool sendCommand (const QString &);
    QStringList commands;
    bool m_use_slave : 1;
protected slots:
    virtual void processStopped (KProcess *);
private slots:
    void dataWritten (KProcess *);
};

class MPlayerPreferencesPage;
class MPlayerPreferencesFrame;

/*
 * MPlayer process
 */
class KDE_EXPORT MPlayer : public MPlayerBase {
    Q_OBJECT
public:
    MPlayer (QObject * parent, Settings * settings);
    ~MPlayer ();
    virtual void init ();
    virtual QString menuName () const;
    virtual WId widget ();
    virtual bool grabPicture (const KURL & url, int pos);
    bool run (const char * args, const char * pipe = 0L);
public slots:
    virtual bool play (Source *, NodePtr mrl);
    virtual bool stop ();
    virtual bool pause ();
    virtual bool seek (int pos, bool absolute);
    virtual bool volume (int pos, bool absolute);
    virtual bool saturation (int pos, bool absolute);
    virtual bool hue (int pos, bool absolute);
    virtual bool contrast (int pos, bool absolute);
    virtual bool brightness (int pos, bool absolute);
    MPlayerPreferencesPage * configPage () const { return m_configpage; }
protected slots:
    void processStopped (KProcess *);
private slots:
    void processOutput (KProcess *, char *, int);
private:
    QString m_process_output;
    QString m_grabfile;
    QWidget * m_widget;
    MPlayerPreferencesPage * m_configpage;
    QString m_tmpURL;
    int old_volume;
};

/*
 * MPlayer preferences page
 */
class MPlayerPreferencesPage : public PreferencesPage {
public:
    enum Pattern {
        pat_size = 0, pat_cache, pat_pos, pat_index,
        pat_refurl, pat_ref, pat_start,
        pat_dvdlang, pat_dvdsub, pat_dvdtitle, pat_dvdchapter, pat_vcdtrack,
        pat_last
    };
    MPlayerPreferencesPage (MPlayer *);
    KDE_NO_CDTOR_EXPORT ~MPlayerPreferencesPage () {}
    void write (KConfig *);
    void read (KConfig *);
    void sync (bool fromUI);
    void prefLocation (QString & item, QString & icon, QString & tab);
    QFrame * prefPage (QWidget * parent);
    QRegExp m_patterns[pat_last];
    int cachesize;
    QString additionalarguments;
    bool alwaysbuildindex;
private:
    MPlayer * m_process;
    MPlayerPreferencesFrame * m_configframe;
};

/*
 * Base class for all recorders
 */
class KMPLAYER_EXPORT Recorder {
public:
    KDE_NO_EXPORT const KURL & recordURL () const { return m_recordurl; }
    KDE_NO_EXPORT void setURL (const KURL & url) { m_recordurl = url; }
protected:
    KURL m_recordurl;
};

/*
 * MEncoder recorder
 */
class MEncoder : public MPlayerBase, public Recorder {
    Q_OBJECT
public:
    MEncoder (QObject * parent, Settings * settings);
    ~MEncoder ();
    virtual void init ();
    KDE_NO_EXPORT const KURL & recordURL () const { return m_recordurl; }
public slots:
    virtual bool play (Source *, NodePtr mrl);
    virtual bool stop ();
};

/*
 * MPlayer recorder, runs 'mplayer -dumpstream'
 */
class MPlayerDumpstream : public MPlayerBase, public Recorder {
    Q_OBJECT
public:
    MPlayerDumpstream (QObject * parent, Settings * settings);
    ~MPlayerDumpstream ();
    virtual void init ();
    KDE_NO_EXPORT const KURL & recordURL () const { return m_recordurl; }
public slots:
    virtual bool play (Source *, NodePtr mrl);
    virtual bool stop ();
};

class XMLPreferencesPage;
class XMLPreferencesFrame;

/*
 * Base class for all backend processes having the KMPlayer::Backend interface
 */
class KMPLAYER_EXPORT CallbackProcess : public Process {
    Q_OBJECT
    friend class Callback;
public:
    CallbackProcess (QObject * parent, Settings * settings, const char * n, const QString & menu);
    ~CallbackProcess ();
    virtual void setStatusMessage (const QString & msg);
    virtual void setErrorMessage (int code, const QString & msg);
    virtual void setFinished ();
    virtual void setPlaying ();
    virtual void setStarted (QCString dcopname, QByteArray & data);
    virtual void setMovieParams (int length, int width, int height, float aspect);
    virtual void setMoviePosition (int position);
    virtual void setLoadingProgress (int percentage);
    virtual QString menuName () const;
    virtual WId widget ();
    KDE_NO_EXPORT QByteArray & configData () { return m_configdata; }
    KDE_NO_EXPORT bool haveConfig () { return m_have_config == config_yes; }
    bool getConfigData ();
    void setChangedData (const QByteArray &);
    QString dcopName ();
    NodePtr configDocument () { return configdoc; }
    void initProcess (Viewer *);
public slots:
    bool play (Source *, NodePtr mrl);
    bool stop ();
    bool quit ();
    bool pause ();
    bool seek (int pos, bool absolute);
    bool volume (int pos, bool absolute);
    bool saturation (int pos, bool absolute);
    bool hue (int pos, bool absolute);
    bool contrast (int pos, bool absolute);
    bool brightness (int pos, bool absolute);
signals:
    void configReceived ();
protected slots:
    void processStopped (KProcess *);
    void processOutput (KProcess *, char *, int);
protected:
    Callback * m_callback;
    Backend_stub * m_backend;
    QString m_menuname;
    QByteArray m_configdata;
    QByteArray m_changeddata;
    XMLPreferencesPage * m_configpage;
    NodePtr configdoc;
    bool in_gui_update;
    enum { config_unknown, config_probe, config_yes, config_no } m_have_config;
    enum { send_no, send_try, send_new } m_send_config;
};

/*
 * Config document as used by kxineplayer backend
 */
struct KMPLAYER_EXPORT ConfigDocument : public Document {
    ConfigDocument ();
    ~ConfigDocument ();
    NodePtr childFromTag (const QString & tag);
};

/*
 * Element for ConfigDocument
 */
struct KMPLAYER_EXPORT ConfigNode : public DarkNode {
    ConfigNode (NodePtr & d, const QString & tag);
    KDE_NO_CDTOR_EXPORT ~ConfigNode () {}
    NodePtr childFromTag (const QString & tag);
    QWidget * w;
};

/*
 * Element for ConfigDocument, defining type of config item
 */
struct KMPLAYER_EXPORT TypeNode : public ConfigNode {
    TypeNode (NodePtr & d, const QString & t);
    KDE_NO_CDTOR_EXPORT ~TypeNode () {}
    NodePtr childFromTag (const QString & tag);
    void changedXML (QTextStream & out);
    QWidget * createWidget (QWidget * parent);
    const char * nodeName () const { return tag.ascii (); }
    QString tag;
};

/*
 * Preference page for XML type of docuement
 */
class KMPLAYER_EXPORT XMLPreferencesPage : public PreferencesPage {
public:
    XMLPreferencesPage (CallbackProcess *);
    ~XMLPreferencesPage ();
    void write (KConfig *);
    void read (KConfig *);
    void sync (bool fromUI);
    void prefLocation (QString & item, QString & icon, QString & tab);
    QFrame * prefPage (QWidget * parent);
private:
    CallbackProcess * m_process;
    XMLPreferencesFrame * m_configframe;
};

/*
 * Xine backend process
 */
class Xine : public CallbackProcess {
    Q_OBJECT
public:
    Xine (QObject * parent, Settings * settings);
    ~Xine ();
public slots:
    bool ready (Viewer *);
};

/*
 * GStreamer backend process
 */
class GStreamer : public CallbackProcess {
    Q_OBJECT
public:
    GStreamer (QObject * parent, Settings * settings);
    ~GStreamer ();
public slots:
    virtual bool ready (Viewer *);
};

/*
 * ffmpeg backend recorder
 */
class KMPLAYER_EXPORT FFMpeg : public Process, public Recorder {
    Q_OBJECT
public:
    FFMpeg (QObject * parent, Settings * settings);
    ~FFMpeg ();
    virtual void init ();
    void setArguments (const QString & args) { arguments = args; }
public slots:
    virtual bool play (Source *, NodePtr mrl);
    virtual bool stop ();
private slots:
    void processStopped (KProcess *);
private:
    QString arguments;
};

} // namespace

#endif //_KMPLAYERPROCESS_H_
