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

#ifdef KDE_USE_FINAL
#undef Always
#endif
#include <qapplication.h>
#include <qcstring.h>
#include <qcursor.h>
#include <qtimer.h>
#include <qpair.h>
#include <qpushbutton.h>
#include <qpopupmenu.h>
#include <qslider.h>
#include <qvaluelist.h>

#include <kmessagebox.h>
#include <kaboutdata.h>
#include <kdebug.h>
#include <kbookmarkmenu.h>
#include <kconfig.h>
#include <kaction.h>
#include <kprocess.h>
#include <kstandarddirs.h>

#include "kmplayerpartbase.h"
#include "kmplayerview.h"
#include "kmplayerconfig.h"
#include "kmplayerprocess.h"

class KMPlayerBookmarkOwner : public KBookmarkOwner {
public:
    KMPlayerBookmarkOwner (KMPlayer *);
    virtual void openBookmarkURL(const QString& _url);
    virtual QString currentTitle() const;
    virtual QString currentURL() const;
private:
    KMPlayer * m_player;
};

KMPlayerBookmarkOwner::KMPlayerBookmarkOwner (KMPlayer * player)
    : m_player (player) {}

void KMPlayerBookmarkOwner::openBookmarkURL (const QString & url) {
    m_player->openURL (KURL (url));
}

QString KMPlayerBookmarkOwner::currentTitle () const {
    return m_player->process ()->source ()->url ().url ();
}

QString KMPlayerBookmarkOwner::currentURL () const {
    return m_player->process ()->source ()->url ().url ();
}

KMPlayerBookmarkManager::KMPlayerBookmarkManager()
  : KBookmarkManager (locateLocal ("data", "kmplayer/bookmarks.xml"), false) {
}

KMPlayer::KMPlayer (QWidget * wparent, const char *wname,
                    QObject * parent, const char *name, KConfig * config)
 : KMediaPlayer::Player (wparent, wname, parent, name),
   m_config (config),
   m_view (new KMPlayerView (wparent)),
   m_settings (new KMPlayerSettings (this, config)),
   m_process (0L),
   m_mplayer (new MPlayer (this)),
   m_mencoder (new MEncoder (this)),
   m_xine (new Xine (this)),
   m_urlsource (new KMPlayerURLSource (this)),
   m_bookmark_manager (new KMPlayerBookmarkManager),
   m_bookmark_owner (new KMPlayerBookmarkOwner (this)),
   m_autoplay (true),
   m_ispart (false),
   m_noresize (false) {
}

void KMPlayer::showConfigDialog () {
    m_settings->show ();
}

void KMPlayer::init () {
    m_view->init ();
    m_settings->readConfig ();
    new KBookmarkMenu (m_bookmark_manager, m_bookmark_owner,
                       m_view->bookmarkMenu (), 0L, true, true);
    setProcess (m_mplayer);
    m_bPosSliderPressed = false;
    m_view->contrastSlider ()->setValue (m_settings->contrast);
    m_view->brightnessSlider ()->setValue (m_settings->brightness);
    m_view->hueSlider ()->setValue (m_settings->hue);
    m_view->saturationSlider ()->setValue (m_settings->saturation);
    connect (m_view->backButton (), SIGNAL (clicked ()), this, SLOT (back ()));
    connect (m_view->playButton (), SIGNAL (clicked ()), this, SLOT (play ()));
    connect (m_view->forwardButton (), SIGNAL (clicked ()), this, SLOT (forward ()));
    connect (m_view->pauseButton (), SIGNAL (clicked ()), this, SLOT (pause ()));
    connect (m_view->stopButton (), SIGNAL (clicked ()), this, SLOT (stop ()));
    connect (m_view->recordButton(), SIGNAL (clicked()), this, SLOT (record()));
    connect (m_view->positionSlider (), SIGNAL (valueChanged (int)), this, SLOT (positonValueChanged (int)));
    connect (m_view->positionSlider (), SIGNAL (sliderPressed()), this, SLOT (posSliderPressed()));
    connect (m_view->positionSlider (), SIGNAL (sliderReleased()), this, SLOT (posSliderReleased()));
    connect (m_view->contrastSlider (), SIGNAL (valueChanged(int)), this, SLOT (contrastValueChanged(int)));
    connect (m_view->brightnessSlider (), SIGNAL (valueChanged(int)), this, SLOT (brightnessValueChanged(int)));
    connect (m_view->hueSlider (), SIGNAL (valueChanged(int)), this, SLOT (hueValueChanged(int)));
    connect (m_view->saturationSlider (), SIGNAL (valueChanged(int)), this, SLOT (saturationValueChanged(int)));
    connect (m_view, SIGNAL (urlDropped (const KURL &)), this, SLOT (openURL (const KURL &)));
    m_view->popupMenu ()->connectItem (KMPlayerView::menu_config,
                                       m_settings, SLOT (show ()));
    connect (m_mencoder, SIGNAL (started()), this, SLOT (recordingStarted()));
    connect (m_mencoder, SIGNAL (finished()), this, SLOT (recordingFinished()));
    //connect (m_view->configButton (), SIGNAL (clicked ()), m_settings, SLOT (show ()));
}

KMPlayer::~KMPlayer () {
    kdDebug() << "KMPlayer::~KMPlayer" << endl;
    if (!m_ispart)
        delete (KMPlayerView*) m_view;
    m_view = (KMPlayerView*) 0;
    stop ();
    delete m_settings;
    delete m_bookmark_manager;
    delete m_bookmark_owner;
}

KMediaPlayer::View* KMPlayer::view () {
    return m_view;
}

void KMPlayer::setProcess (KMPlayerProcess * process) {
    if (m_process == process)
        return;
    KMPlayerSource * source = m_urlsource;
    if (m_process) {
        m_process->stop ();
        source = m_process->source ();
        disconnect (m_process, SIGNAL (started ()),
                    this, SLOT (processStarted ()));
        disconnect (m_process, SIGNAL (finished ()),
                    this, SLOT (processFinished ()));
        disconnect (m_process, SIGNAL (positionChanged (int)),
                    this, SLOT (processPosition (int)));
        disconnect (m_process, SIGNAL (loading (int)),
                    this, SLOT (processLoading (int)));
        disconnect (m_process, SIGNAL (startPlaying ()),
                    this, SLOT (processPlaying ()));
    }
    m_process = process;
    m_process->setSource (source);
    connect (m_process, SIGNAL (started ()), this, SLOT (processStarted ()));
    connect (m_process, SIGNAL (finished ()), this, SLOT (processFinished ()));
    connect (m_process, SIGNAL (positionChanged (int)),
             this, SLOT (processPosition (int)));
    connect (m_process, SIGNAL (loading (int)),
             this, SLOT (processLoading (int)));
    connect (m_process, SIGNAL (startPlaying ()),
             this, SLOT (processPlaying ()));
}

extern const char * strUrlBackend;
extern const char * strMPlayerGroup;

void KMPlayer::setXine (int id) {
    bool playing = m_process->playing ();
    if (playing)
        m_process->source ()->deactivate ();
    m_settings->urlbackend = QString ("Xine");
    m_config->setGroup (strMPlayerGroup);
    m_config->writeEntry (strUrlBackend, m_settings->urlbackend);
    m_config->sync ();
    setProcess (m_xine);
    QPopupMenu * menu = m_view->playerMenu ();
    for (unsigned i = 0; i < menu->count(); i++) {
        int menuid = menu->idAt (i);
        menu->setItemChecked (menuid, menuid == id);
    }
    if (playing)
        m_process->source ()->activate ();
}

void KMPlayer::setMPlayer (int id) {
    bool playing = m_process->playing ();
    if (playing)
        m_process->source ()->deactivate ();
    m_settings->urlbackend = QString ("MPlayer");
    m_config->setGroup (strMPlayerGroup);
    m_config->writeEntry (strUrlBackend, m_settings->urlbackend);
    m_config->sync ();
    setProcess (m_mplayer);
    QPopupMenu * menu = m_view->playerMenu ();
    for (unsigned i = 0; i < menu->count(); i++) {
        int menuid = menu->idAt (i);
        menu->setItemChecked (menuid, menuid == id);
    }
    if (playing)
        m_process->source ()->activate ();
}

void KMPlayer::setSource (KMPlayerSource * source) {
    KMPlayerSource * oldsource = m_process->source ();
    if (oldsource) {
        oldsource->deactivate ();
        closeURL ();
    }
    m_process->setSource (source);
    m_mencoder->setSource (source);
    if (source->hasLength () && m_settings->showposslider)
        m_view->positionSlider()->show ();
    else
        m_view->positionSlider()->hide ();
    if (source->isSeekable ()) {
        m_view->forwardButton ()->show ();
        m_view->backButton ()->show ();
    } else {
        m_view->forwardButton ()->hide ();
        m_view->backButton ()->hide ();
    }
    if (source) QTimer::singleShot (0, source, SLOT (activate ()));
    emit sourceChanged (source);
}

bool KMPlayer::isSeekable (void) const {
    return m_process->source ()->isSeekable ();
}

bool KMPlayer::hasLength () const {
    return m_process->source ()->hasLength (); 
}

unsigned long KMPlayer::length () const {
    return m_process->source ()->length ();
}
            
bool KMPlayer::openURL (const KURL & url) {
    kdDebug () << "KMPlayer::openURL " << url.url() << url.isValid () << endl;
    if (!m_view || !url.isValid ()) return false;
    m_urlsource->setURL (url);
    setSource (m_urlsource);
    return true;
}

bool KMPlayer::closeURL () {
    stop ();
    if (!m_view) return false;
    m_view->viewer ()->setAspect (0.0);
    m_view->reset ();
    return true;
}

bool KMPlayer::openFile () {
    return false;
}

void KMPlayer::keepMovieAspect (bool b) {
    if (!m_view) return;
    m_view->setKeepSizeRatio (b);
    m_view->viewer ()->setAspect (b ? m_process->source ()->aspect () : 0.0);
}

void KMPlayer::recordingStarted () {
    if (!m_view) return;
    if (!m_view->recordButton ()->isOn ()) 
        m_view->recordButton ()->toggle ();
}

void KMPlayer::recordingFinished () {
    if (!m_view) return;
    if (m_view->recordButton ()->isOn ()) 
        m_view->recordButton ()->toggle ();
    if (m_settings->autoplayafterrecording)
        openURL (m_mencoder->recordURL ());
}

void KMPlayer::processFinished () {
    kdDebug () << "process finished" << endl;
    if (m_process->source ()->position () > m_process->source ()->length ())
        m_process->source ()->setLength (m_process->source ()->position ());
    m_process->source ()->setPosition (0);
    if (m_view && m_view->playButton ()->isOn ()) {
        m_view->playButton ()->toggle ();
        m_view->positionSlider()->setEnabled (false);
        m_view->positionSlider()->setValue (0);
    }
    if (m_view) {
        m_view->reset ();
        emit finished ();
    }
}

void KMPlayer::processStarted () {
    if (!m_view) return;
    if (m_settings->showposslider && m_process->source ()->hasLength ())
        m_view->positionSlider()->show();
    else
        m_view->positionSlider()->hide();
    if (!m_view->playButton ()->isOn ()) m_view->playButton ()->toggle ();
    m_view->positionSlider()->setEnabled (true);
}

void KMPlayer::processPosition (int pos) {
    if (!m_view) return;
    QSlider *slider = m_view->positionSlider ();
    if (m_process->source ()->length () <= 0 && pos > 7 * slider->maxValue ()/8)
        slider->setMaxValue (slider->maxValue() * 2);
    else if (slider->maxValue() < pos)
        slider->setMaxValue (int (1.4 * slider->maxValue()));
    if (!m_bPosSliderPressed)
        slider->setValue (pos);
}

void KMPlayer::processLoading (int percentage) {
    emit loading (percentage);
}

void KMPlayer::processPlaying () {
    if (!m_view) return;
    kdDebug () << "KMPlayer::processPlaying " << endl;
    if (m_settings->sizeratio)
        m_view->viewer ()->setAspect (m_process->source ()->aspect ());
    emit loading (100);
}

unsigned long KMPlayer::position () const {
    return 100 * m_process->source ()->position ();
}

void KMPlayer::pause () {
    m_process->pause ();
}

void KMPlayer::back () {
    m_process->seek (-1 * m_settings->seektime * 10, false);
}

void KMPlayer::forward () {
    m_process->seek (m_settings->seektime * 10, false);
}

void KMPlayer::record () {
    if (m_view) m_view->setCursor (QCursor (Qt::WaitCursor));
    if (m_mencoder->playing ()) {
        m_mencoder->stop ();
    } else {
        m_process->stop ();
        m_settings->show (KMPlayerPreferences::PageRecording);
        if (m_view->recordButton ()->isOn ()) 
            m_view->recordButton ()->toggle ();
    }
    if (m_view) m_view->setCursor (QCursor (Qt::ArrowCursor));
}

void KMPlayer::play () {
    m_process->play ();
    if (m_process->playing () && !m_view->playButton ()->isOn ())
        m_view->playButton ()->toggle ();
    else if (!m_process->playing () && m_view->playButton ()->isOn ())
        m_view->playButton ()->toggle ();
}

bool KMPlayer::playing () const {
    return m_process && m_process->playing ();
}

void KMPlayer::stop () {
    if (m_view && !m_view->stopButton ()->isOn ())
        m_view->stopButton ()->toggle ();
    if (m_view) m_view->setCursor (QCursor (Qt::WaitCursor));
    m_process->stop ();
    if (m_view) m_view->setCursor (QCursor (Qt::ArrowCursor));
    if (m_view && m_view->stopButton ()->isOn ())
        m_view->stopButton ()->toggle ();
}

void KMPlayer::seek (unsigned long msec) {
    m_process->seek (msec/100, true);
}

void KMPlayer::adjustVolume (int incdec) {
    m_process->volume (incdec, false);
}

void KMPlayer::sizes (int & w, int & h) const {
    if (m_noresize) {
        w = m_view->viewer ()->width ();
        h = m_view->viewer ()->height ();
    } else {
        w = m_process->source ()->width ();
        h = m_process->source ()->height ();
    }
}

void KMPlayer::posSliderPressed () {
    m_bPosSliderPressed=true;
}

void KMPlayer::posSliderReleased () {
    m_bPosSliderPressed=false;
    m_process->seek (m_view->positionSlider()->value(), true);
}

void KMPlayer::contrastValueChanged (int val) {
    m_settings->contrast = val;
    m_process->contrast (val, true);
}

void KMPlayer::brightnessValueChanged (int val) {
    m_settings->brightness = val;
    m_process->brightness (val, true);
}

void KMPlayer::hueValueChanged (int val) {
    m_settings->hue = val;
    m_process->hue (val, true);
}

void KMPlayer::saturationValueChanged (int val) {
    m_settings->saturation = val;
    m_process->saturation (val, true);
}

void KMPlayer::positonValueChanged (int /*pos*/) {
    if (!m_bPosSliderPressed)
        return;
}

KAboutData* KMPlayer::createAboutData () {
    KMessageBox::error(0L, "createAboutData", "KMPlayer");
    return 0;
}

//-----------------------------------------------------------------------------

KMPlayerSource::KMPlayerSource (KMPlayer * player)
    : QObject (player), m_player (player) {
    kdDebug () << "KMPlayerSource::KMPlayerSource" << endl;
}

KMPlayerSource::~KMPlayerSource () {
    kdDebug () << "KMPlayerSource::~KMPlayerSource" << endl;
}

void KMPlayerSource::init () {
    m_width = 0;
    m_height = 0;
    m_aspect = 0.0;
    setLength (0);
    m_position = 0;
    m_identified = false;
    m_recordcmd.truncate (0);
}

void KMPlayerSource::setLength (int len) {
    m_length = len;
    KMPlayerView * view = static_cast <KMPlayerView*> (m_player->view ());
    if (view)
        view->positionSlider()->setMaxValue (len > 0 ? len + 9 : 300);
}

bool KMPlayerSource::processOutput (const QString & str) {
    if (m_identified)
        return false;
    if (str.startsWith ("ID_VIDEO_WIDTH")) {
        int pos = str.find ('=');
        if (pos > 0)
            setWidth (str.mid (pos + 1).toInt());
        kdDebug () << "KMPlayerSource::processOutput " << width() << endl;
    } else if (str.startsWith ("ID_VIDEO_HEIGHT")) {
        int pos = str.find ('=');
        if (pos > 0)
            setHeight (str.mid (pos + 1).toInt());
    } else if (str.startsWith ("ID_VIDEO_ASPECT")) {
        int pos = str.find ('=');
        if (pos > 0)
            setAspect (str.mid (pos + 1).replace (',', '.').toFloat ());
    } else if (str.startsWith ("ID_LENGTH")) {
        int pos = str.find ('=');
        if (pos > 0)
            setLength (10 * str.mid (pos + 1).toInt());
    } else
        return false;
    return true;
}

QString KMPlayerSource::filterOptions () {
    KMPlayerSettings* m_settings = m_player->settings ();
    QString PPargs ("");
    if (m_settings->postprocessing)
    {
        if (m_settings->pp_default)
            PPargs = "-vop pp=de";
        else if (m_settings->pp_fast)
            PPargs = "-vop pp=fa";
        else if (m_settings->pp_custom) {
            PPargs = "-vop pp=";
            if (m_settings->pp_custom_hz) {
                PPargs += "hb";
                if (m_settings->pp_custom_hz_aq && \
                        m_settings->pp_custom_hz_ch)
                    PPargs += ":ac";
                else if (m_settings->pp_custom_hz_aq)
                    PPargs += ":a";
                else if (m_settings->pp_custom_hz_ch)
                    PPargs += ":c";
                PPargs += "/";
            }
            if (m_settings->pp_custom_vt) {
                PPargs += "vb";
                if (m_settings->pp_custom_vt_aq && \
                        m_settings->pp_custom_vt_ch)
                    PPargs += ":ac";
                else if (m_settings->pp_custom_vt_aq)
                    PPargs += ":a";
                else if (m_settings->pp_custom_vt_ch)
                    PPargs += ":c";
                PPargs += "/";
            }
            if (m_settings->pp_custom_dr) {
                PPargs += "dr";
                if (m_settings->pp_custom_dr_aq && \
                        m_settings->pp_custom_dr_ch)
                    PPargs += ":ac";
                else if (m_settings->pp_custom_dr_aq)
                    PPargs += ":a";
                else if (m_settings->pp_custom_dr_ch)
                    PPargs += ":c";
                PPargs += "/";
            }
            if (m_settings->pp_custom_al) {
                PPargs += "al";
                if (m_settings->pp_custom_al_f)
                    PPargs += ":f";
                PPargs += "/";
            }
            if (m_settings->pp_custom_tn) {
                PPargs += "tn";
                /*if (1 <= m_settings->pp_custom_tn_s <= 3){
                    PPargs += ":";
                    PPargs += m_settings->pp_custom_tn_s;
                    }*/ //disabled 'cos this is wrong
                PPargs += "/";
            }
            if (m_settings->pp_lin_blend_int) {
                PPargs += "lb";
                PPargs += "/";
            }
            if (m_settings->pp_lin_int) {
                PPargs += "li";
                PPargs += "/";
            }
            if (m_settings->pp_cub_int) {
                PPargs += "ci";
                PPargs += "/";
            }
            if (m_settings->pp_med_int) {
                PPargs += "md";
                PPargs += "/";
            }
            if (m_settings->pp_ffmpeg_int) {
                PPargs += "fd";
                PPargs += "/";
            }
        }
        if (PPargs.endsWith("/"))
            PPargs.truncate(PPargs.length()-1);
    }
    return PPargs;
}

QString KMPlayerSource::ffmpegCommand () {
    if (m_ffmpegCommand.isEmpty ())
        return QString::null;
    return QString ("ffmpeg ") + QString (" ") + m_ffmpegCommand;
}

bool KMPlayerSource::hasLength () {
    return true;
}

bool KMPlayerSource::isSeekable () {
    return true;
}

void KMPlayerSource::setIdentified (bool b) {
    kdDebug () << "KMPlayerSource::setIdentified " << m_identified << b <<endl;
    m_identified = b;
}

QString KMPlayerSource::prettyName () {
    return QString (i18n ("Unknown"));
}
//-----------------------------------------------------------------------------

KMPlayerURLSource::KMPlayerURLSource (KMPlayer * player, const KURL & url)
    : KMPlayerSource (player) {
    m_url = url;
    kdDebug () << "KMPlayerURLSource::KMPlayerURLSource" << endl;
}

KMPlayerURLSource::~KMPlayerURLSource () {
    kdDebug () << "KMPlayerURLSource::~KMPlayerURLSource" << endl;
}

void KMPlayerURLSource::init () {
    KMPlayerSource::init ();
    isreference = false;
    m_urls.clear ();
    m_urlother = KURL ();
#ifdef HAVE_XINE
    KMPlayerView * view = static_cast <KMPlayerView*> (m_player->view ());
    QPopupMenu * menu = view->playerMenu ();
    menu->clear ();
    menu->insertItem (i18n ("&MPlayer"), m_player, SLOT (setMPlayer (int)));
    menu->insertItem (i18n ("&Xine"), m_player, SLOT (setXine (int)));
    menu->setEnabled (true);
    if (m_player->settings ()->urlbackend == QString ("Xine")) {
        menu->setItemChecked (menu->idAt (1), true);
        m_player->setProcess (m_player->xine ());
    } else {
        m_player->setProcess (m_player->mplayer ());
        menu->setItemChecked (menu->idAt (0), true);
    }
    view->popupMenu ()->setItemVisible (KMPlayerView::menu_player, true);
#endif
}

bool KMPlayerURLSource::hasLength () {
    return !!length ();
}

void KMPlayerURLSource::setURL (const KURL & url) { 
    m_url = url;
    kdDebug () << "KMPlayerURLSource::setURL:" << url.url () << endl;
    isreference = false;
    m_identified = false;
    m_urls.clear ();
    m_urlother = KURL ();
}

bool KMPlayerURLSource::processOutput (const QString & str) {
    if (m_identified)
        return false;
    if (str.startsWith ("ID_FILENAME")) {
        int pos = str.find ('=');
        if (pos < 0) 
            return false;
        KURL url (str.mid (pos + 1));
        if (url.isValid ())
            m_urls.push_front (url);
        kdDebug () << "KMPlayerURLSource::processOutput ID_FILENAME=" << url.url () << endl;
        return true;
    } else if (str.startsWith ("Playing")) {
        KURL url(str.mid (8));
        if (url.isValid ()) {
            if (!isreference && !m_urlother.isEmpty ()) {
                m_urls.push_back (m_urlother);
                return true;
            }
            m_urlother = url;
            isreference = false;
            kdDebug () << "KMPlayerURLSource::processOutput " << m_url.url () << endl;
            return true;
        }
    } else if (str.find ("Reference Media file") >= 0) {
        isreference = true;
    }
    return KMPlayerSource::processOutput (str);
}

void KMPlayerURLSource::buildArguments () {
    m_recordcmd = QString ("");
    if (m_url.isLocalFile ()) {
        QString myurl (KProcess::quote (m_url.isLocalFile () ? m_url.path () : m_url.url ()));
        m_ffmpegCommand = QString ("-i ") + myurl;
    } else
        m_ffmpegCommand.truncate (0);
}

void KMPlayerURLSource::activate () {
    init ();
    buildArguments ();
    if (url ().isEmpty ())
        return;
    QTimer::singleShot (0, m_player, SLOT (play ()));
    //if (m_player->liveconnectextension ())
    //    m_player->liveconnectextension ()->enableFinishEvent ();
}


void KMPlayerURLSource::setIdentified (bool b) {
    KMPlayerSource::setIdentified (b);
    if (!isreference && !m_urlother.isEmpty ())
        m_urls.push_back (m_urlother);
    if (m_urls.count () > 0) {
        m_url = *m_urls.begin ();
        kdDebug () << "KMPlayerURLSource::setIdentified new url:" << m_url.prettyURL() << endl;
    }
    m_urlother = KURL ();
    buildArguments ();
    int cache = m_player->settings ()->cachesize;
    if (!m_url.isLocalFile () && cache > 0 && 
            m_url.protocol () != QString ("dvd") &&
            m_url.protocol () != QString ("vcd") &&
            m_url.protocol () != QString ("tv"))
        m_options.sprintf ("-cache %d ", cache);

    if (m_player->settings ()->alwaysbuildindex && m_url.isLocalFile ()) {
        if (m_url.path ().lower ().endsWith (".avi") ||
                m_url.path ().lower ().endsWith (".divx")) {
            m_options += QString (" -idx ");
            m_recordcmd = QString (" -idx ");
        }
    }
}

void KMPlayerURLSource::deactivate () {
#ifdef HAVE_XINE
    KMPlayerView * view = static_cast <KMPlayerView*> (m_player->view ());
    if (view)
        view->popupMenu ()->setItemVisible (KMPlayerView::menu_player, false);
#endif
}

QString KMPlayerURLSource::prettyName () {
    return QString (i18n ("URL"));
}

#include "kmplayerpartbase.moc"
#include "kmplayersource.moc"
