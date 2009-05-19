/**
 * Copyright (C) 2002-2003 by Koos Vriezen <koos.vriezen@gmail.com>
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
 *  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 **/

#include <stdio.h>
#include <math.h>

#include "config-kmplayer.h"
// include files for Qt
#include <qstyle.h>
#include <qtimer.h>
#include <qpainter.h>
#include <qmetaobject.h>
#include <qlayout.h>
#include <qpixmap.h>
#include <qtextedit.h>
#include <qtooltip.h>
#include <qapplication.h>
#include <qiconset.h>
#include <qcursor.h>
#include <qkeysequence.h>
#include <qslider.h>
#include <qlabel.h>
#include <qdatastream.h>
#include <QContextMenuEvent>
#include <Q3TextDrag>
#include <QDropEvent>
#include <QAction>
#include <QDragEnterEvent>
#include <QTextDocument>
#include <QTextCursor>
#include <qcursor.h>
#include <qclipboard.h>
#include <QMainWindow>
#include <QDockWidget>

#include <kiconloader.h>
#include <kstatusbar.h>
#include <kdebug.h>
#include <klocale.h>
#include <kapplication.h>
#include <kactioncollection.h>
#include <kstdaction.h>
#include <kshortcut.h>
#include <kfinddialog.h>
#include <kglobalsettings.h>
#include <k3staticdeleter.h>

#include "kmplayerview.h"
#include "kmplayercontrolpanel.h"
#include "kmplayersource.h"
#include "playlistview.h"
#include "viewarea.h"
#include <solid/powermanagement.h>

extern const char * normal_window_xpm[];
extern const char * playlist_xpm[];


/* mouse invisible: define the time (in 1/1000 seconds) before mouse goes invisible */


using namespace KMPlayer;

//-------------------------------------------------------------------------

KDE_NO_CDTOR_EXPORT PictureWidget::PictureWidget (QWidget * parent, View * view)
 : QWidget (parent), m_view (view) {
    setAutoFillBackground (true);
}

KDE_NO_EXPORT void PictureWidget::mousePressEvent (QMouseEvent *) {
    m_view->emitPictureClicked ();
}

KDE_NO_EXPORT void PictureWidget::mouseMoveEvent (QMouseEvent *e) {
    if (e->state () == Qt::NoButton)
        m_view->mouseMoved (e->x (), e->y ());
}

//-----------------------------------------------------------------------------

KDE_NO_CDTOR_EXPORT TextEdit::TextEdit (QWidget * parent, View * view) : QTextEdit (parent), m_view (view) {
#if QT_VERSION > 0x040399
    setAttribute (Qt::WA_NativeWindow);
#endif
    setReadOnly (true);
    QPalette p=palette();
    p.setColor (QPalette::Active, QPalette::Base, QColor (Qt::black));
    p.setColor (QPalette::Active, QPalette::Foreground, (QColor (0xB2, 0xB2, 0xB2)));
    setPalette (p);
}

KDE_NO_EXPORT void TextEdit::contextMenuEvent (QContextMenuEvent * e) {
    m_view->controlPanel ()->popupMenu->exec (e->globalPos ());
}

//-----------------------------------------------------------------------------

KDE_NO_CDTOR_EXPORT InfoWindow::InfoWindow (QWidget * parent, View * view)
 : /*QTextEdit (parent),*/ m_view (view) {
    setReadOnly (true);
    //setLinkUnderline (false);
}

KDE_NO_EXPORT void InfoWindow::contextMenuEvent (QContextMenuEvent * e) {
    m_view->controlPanel ()->popupMenu->exec (e->globalPos ());
}

//-----------------------------------------------------------------------------

KDE_NO_CDTOR_EXPORT View::View (QWidget *parent)
  : KMediaPlayer::View (parent),
    m_control_panel (0L),
    m_status_bar (0L),
    m_volume_slider (0L),
    m_mixer_object ("kicker"),
    m_controlpanel_mode (CP_Show),
    m_old_controlpanel_mode (CP_Show),
    m_statusbar_mode (SB_Hide),
    controlbar_timer (0),
    infopanel_timer (0),
    m_powerManagerStopSleep( -1 ),
    m_keepsizeratio (false),
    m_playing (false),
    m_mixer_init (false),
    m_inVolumeUpdate (false),
    m_tmplog_needs_eol (false),
    m_revert_fullscreen (false),
    m_no_info (false),
    m_edit_mode (false)
{}

KDE_NO_EXPORT void View::dropEvent (QDropEvent * de) {
    KUrl::List uris = KUrl::List::fromMimeData( de->mimeData() );
    if (uris.isEmpty() && Q3TextDrag::canDecode (de)) {
        QString text;
        Q3TextDrag::decode (de, text);
        uris.push_back (KURL (text));
    }
    if (uris.size () > 0) {
        for (int i = 0; i < uris.size (); i++)
            uris [i] = KURL::decode_string (uris [i].url ());
        //m_widgetstack->currentWidget ()->setFocus ();
        emit urlDropped (uris);
        de->accept ();
    }
}

KDE_NO_EXPORT void View::dragEnterEvent (QDragEnterEvent* dee) {
    if (isDragValid (dee))
        dee->accept ();
}

void View::initDock (QWidget *central) {
    m_dockarea = new QMainWindow;
    m_dockarea->setCentralWidget (central);
    central->setVisible (true);

    m_dock_playlist = new QDockWidget (i18n ("Playlist"));
    if (central != m_playlist)
        m_dock_playlist->setWidget (m_playlist);
    m_dock_playlist->setObjectName ("playlist");

    m_dock_infopanel = new QDockWidget (i18n ("Information"));
    if (central != m_infopanel)
        m_dock_infopanel->setWidget (m_infopanel);
    m_dock_infopanel->setObjectName ("infopanel");

    m_dock_playlist->hide ();
    m_dock_infopanel->hide ();

    m_dockarea->addDockWidget (Qt::BottomDockWidgetArea, m_dock_infopanel);
    m_dockarea->addDockWidget (Qt::LeftDockWidgetArea, m_dock_playlist);

    layout ()->addWidget (m_dockarea);

    m_dockarea->setWindowFlags (Qt::SubWindow);
    m_dockarea->show ();

    m_view_area->resizeEvent (0L);
}

KDE_NO_EXPORT void View::init (KActionCollection * action_collection) {
    setAutoFillBackground (false); // prevents flashing
    QPalette pal (QColor (64, 64,64), QColor (32, 32, 32));
    QVBoxLayout * viewbox = new QVBoxLayout;
    viewbox->setContentsMargins (0, 0, 0, 0);
    setLayout (viewbox);
    m_view_area = new ViewArea (NULL, this);
    m_playlist = new PlayListView (NULL, this, action_collection);

    m_picture = new PictureWidget (m_view_area, this);
    m_picture->hide ();
    m_control_panel = new ControlPanel (m_view_area, this);
    m_control_panel->setMaximumSize (2500, controlPanel ()->maximumSize ().height ());
    m_status_bar = new StatusBar (m_view_area);
    m_status_bar->insertItem (QString (""), 0);
    m_status_bar->setItemAlignment (0, Qt::AlignLeft);
    m_status_bar->setSizeGripEnabled (false);
    m_status_bar->setAutoFillBackground (true);
    QSize sbsize = m_status_bar->sizeHint ();
    m_status_bar->hide ();
    m_status_bar->setMaximumSize (2500, sbsize.height ());
    setVideoWidget (m_view_area);

    m_multiedit = new TextEdit (m_view_area, this);
    QFont fnt = KGlobalSettings::fixedFont ();
    m_multiedit->setFont (fnt);
    m_multiedit->hide ();

    m_infopanel = new InfoWindow (NULL, this);

    connect (m_control_panel->scale_slider, SIGNAL (valueChanged (int)),
             m_view_area, SLOT (scale (int)));
    setFocusPolicy (Qt::ClickFocus);

    setAcceptDrops (true);
}

KDE_NO_CDTOR_EXPORT View::~View () {
    if (m_view_area->parent () != this)
        delete m_view_area;
}

KDE_NO_EXPORT void View::setEraseColor (const QColor & color) {
    /*KMediaPlayer::View::setEraseColor (color);
    if (statusBar ()) {
        statusBar ()->setEraseColor (color);
        controlPanel ()->setEraseColor (color);
    }*/
}

void View::setInfoMessage (const QString & msg) {
    bool ismain = m_dockarea->centralWidget () == m_infopanel;
    if (msg.isEmpty ()) {
        if (!ismain && !m_edit_mode && !infopanel_timer)
            infopanel_timer = startTimer (0);
       m_infopanel->clear ();
    } else if (ismain || !m_no_info) {
        if (!ismain && !m_edit_mode && !m_dock_infopanel->isVisible ())
            m_dock_infopanel->show ();
        if (m_edit_mode)
            m_infopanel->setPlainText (msg);
        else
            m_infopanel->setHtml (msg);
    }
}

void View::setStatusMessage (const QString & msg) {
    if (m_statusbar_mode != SB_Hide)
        m_status_bar->changeItem (msg, 0);
}

void View::toggleShowPlaylist () {
    if (m_controlpanel_mode == CP_Only)
        return;
    if (m_dock_playlist->isVisible ())
        m_dock_playlist->hide ();
    else
        m_dock_playlist->show();
    /*if (m_dock_playlist->mayBeShow ()) {
        if (m_dock_playlist->isDockBackPossible ())
            m_dock_playlist->dockBack ();
        else {
            bool horz = true;
            QStyle *style = m_playlist->style ();
            int h = style->pixelMetric (QStyle::PM_ScrollBarExtent, NULL, m_playlist);
            h += style->pixelMetric (QStyle::PM_DockWidgetFrameWidth, NULL, m_playlist);
            h +=style->pixelMetric (QStyle::PM_DockWidgetHandleExtent, NULL, m_playlist);
            for (Q3ListViewItem *i=m_playlist->firstChild();i;i=i->itemBelow()) {
                h += i->height ();
                if (h > int (0.25 * height ())) {
                    horz = false;
                    break;
                }
            }
            int perc = 30;
            if (horz && 100 * h / height () < perc)
                perc = 100 * h / height ();
            m_dock_playlist->manualDock (m_dock_video, horz ? K3DockWidget::DockTop : K3DockWidget::DockLeft, perc);
        }
    } else
        m_dock_playlist->undock ();*/
}

void View::setViewOnly () {
    m_dock_playlist->hide ();
    m_dock_infopanel->hide ();
}

void View::setEditMode (RootPlayListItem *ri, bool enable) {
    m_edit_mode = enable;
    m_infopanel->setReadOnly (!m_edit_mode);
    if (m_edit_mode && !m_dock_infopanel->isVisible ())
        m_dock_infopanel->show ();
    //if (m_edit_mode && m_dock_infopanel->mayBeShow ())
    //    m_dock_infopanel->manualDock(m_dock_video,K3DockWidget::DockBottom,50);
    m_playlist->showAllNodes (ri, m_edit_mode);
}

#ifndef KMPLAYER_WITH_CAIRO
bool View::setPicture (const QString & path) {
    if (path.isEmpty ())
        m_image = QImage ();
    else {
        m_image = QImage (path);
        if (m_image.isNull ())
            kDebug() << "View::setPicture failed " << path;
        else if (m_image.depth () < 24)
            m_image = m_image.convertDepth (32, 0);
    }
    m_picture->setVisible (!m_image.isNull ());
    if (m_image.isNull ()) {
        m_view_area->setVideoWidgetVisible (true);
    } else {
        QPalette palette = m_picture->palette ();
        palette.setColor (m_picture->backgroundRole(), viewArea()->palette ().color (backgroundRole ()));
        palette.setBrush (m_picture->backgroundRole(), QBrush (m_image));
        m_picture->setPalette (palette);
        m_view_area->setVideoWidgetVisible (false);
        controlPanel ()->raise ();
        setControlPanelMode (CP_AutoHide);
    }
    return !m_image.isNull ();
}
#endif

KDE_NO_EXPORT void View::updateVolume () {
    /*if (m_mixer_init && !m_volume_slider)
        return;
    QByteArray data, replydata;
    QCString replyType;
    int volume;
    bool has_mixer = false;
    bool has_mixer = kapp->dcopClient ()->call (m_mixer_object, "Mixer0",
            "masterVolume()", data, replyType, replydata);
    if (!has_mixer) {
        m_mixer_object = "kmix";
        has_mixer = kapp->dcopClient ()->call (m_mixer_object, "Mixer0",
                "masterVolume()", data, replyType, replydata);
    }
    if (has_mixer) {
        QDataStream replystream (replydata, IO_ReadOnly);
        replystream >> volume;
        if (!m_mixer_init) {
            QLabel * mixer_label = new QLabel (i18n ("Volume:"), m_control_panel->popupMenu ());
            m_control_panel->popupMenu ()->insertItem (mixer_label, -1, 4);
            m_volume_slider = new QSlider (0, 100, 10, volume, Qt::Horizontal, m_control_panel->popupMenu ());
            connect(m_volume_slider, SIGNAL(valueChanged(int)), this,SLOT(setVolume(int)));
            m_control_panel->popupMenu ()->insertItem (m_volume_slider, ControlPanel::menu_volume, 5);
            m_control_panel->popupMenu ()->insertSeparator (6);
        } else {
            m_inVolumeUpdate = true;
            m_volume_slider->setValue (volume);
            m_inVolumeUpdate = false;
        }
    } else if (m_volume_slider) {
        m_control_panel->popupMenu ()->removeItemAt (6);
        m_control_panel->popupMenu ()->removeItemAt (5);
        m_control_panel->popupMenu ()->removeItemAt (4);
        m_volume_slider = 0L;
    }*/
    m_mixer_init = true;
}

void View::toggleVideoConsoleWindow () {
    if (m_multiedit->isVisible ()) {
        m_multiedit->hide ();
        m_view_area->setVideoWidgetVisible (true);
        m_control_panel->videoConsoleAction->setIcon (
                KIconLoader::global ()->loadIconSet (
                    QString ("konsole"), KIconLoader::Small, 0, true));
        m_control_panel->videoConsoleAction->setText (i18n ("Con&sole"));
        delayedShowButtons (false);
    } else {
        m_control_panel->videoConsoleAction->setIcon (
                KIconLoader::global ()->loadIconSet (
                    QString ("video"), KIconLoader::Small, 0, true));
        m_control_panel->videoConsoleAction->setText (i18n ("V&ideo"));
        m_multiedit->show ();
        m_multiedit->raise ();
        m_view_area->setVideoWidgetVisible (false);
        addText (QString (""), false);
        if (m_controlpanel_mode == CP_AutoHide && m_playing)
            m_control_panel->show();
    }
    updateLayout ();
    emit windowVideoConsoleToggled (m_multiedit->isVisible ());
}

void View::setControlPanelMode (ControlPanelMode m) {
    killTimer (controlbar_timer);
    controlbar_timer = 0L;
    m_old_controlpanel_mode = m_controlpanel_mode = m;
    if (m_playing && isFullScreen())
        m_controlpanel_mode = CP_AutoHide;
    if ((m_controlpanel_mode == CP_Show || m_controlpanel_mode == CP_Only) &&
            !m_control_panel->isVisible ()) {
        m_control_panel->show ();
    } else if (m_controlpanel_mode == CP_AutoHide) {
        if (!m_image.isNull () || (m_playing && !m_multiedit->isVisible ()))
            delayedShowButtons (false);
        else if (!m_control_panel->isVisible ()) {
            m_control_panel->show ();
        }
    } else if (m_controlpanel_mode == CP_Hide) {
        bool vis = m_control_panel->isVisible();
        m_control_panel->hide ();
        if (vis)
            m_view_area->resizeEvent (0L);
    }
    m_view_area->resizeEvent (0L);
}

void View::setStatusBarMode (StatusBarMode m) {
    m_statusbar_mode = m;
    m_status_bar->setVisible (m != SB_Hide);
    m_view_area->resizeEvent (0L);
}

KDE_NO_EXPORT void View::delayedShowButtons (bool show) {
    if ((show && m_control_panel->isVisible ()) ||
            (!show && !m_control_panel->isVisible ())) {
        if (controlbar_timer) {
            killTimer (controlbar_timer);
            controlbar_timer = 0;
        }
        if (!show)
            m_control_panel->hide (); // for initial race
    } else if (m_controlpanel_mode == CP_AutoHide &&
            (m_playing || !m_image.isNull ()) &&
            !m_multiedit->isVisible () && !controlbar_timer) {
        controlbar_timer = startTimer (500);
    }
}

KDE_NO_EXPORT void View::mouseMoved (int x, int y) {
    int h = m_view_area->height ();
    int vert_buttons_pos = h - statusBarHeight ();
    int cp_height = controlPanel ()->maximumSize ().height ();
    if (cp_height > int (0.25 * h))
        cp_height = int (0.25 * h);
    delayedShowButtons (y > vert_buttons_pos-cp_height && y < vert_buttons_pos);
}

KDE_NO_EXPORT void View::setVolume (int vol) {
    if (m_inVolumeUpdate) return;
    //QByteArray data;
    //QDataStream arg( data, IO_WriteOnly );
    //arg << vol;
    //if (!kapp->dcopClient()->send (m_mixer_object, "Mixer0", "setMasterVolume(int)", data))
    //    kWarning() << "Failed to update volume";
}

KDE_NO_EXPORT void View::updateLayout () {
    if (m_controlpanel_mode == CP_Only)
        m_control_panel->setMaximumSize (2500, height ());
    m_view_area->resizeEvent (0L);
}

void View::setKeepSizeRatio (bool b) {
    if (m_keepsizeratio != b) {
        m_keepsizeratio = b;
        updateLayout ();
        m_view_area->update ();
    }
}

KDE_NO_EXPORT void View::timerEvent (QTimerEvent * e) {
    if (e->timerId () == controlbar_timer) {
        controlbar_timer = 0;
        if (m_playing || !m_image.isNull ()) {
            int vert_buttons_pos = m_view_area->height()-statusBarHeight ();
            QPoint mouse_pos = m_view_area->mapFromGlobal (QCursor::pos ());
            int cp_height = m_control_panel->maximumSize ().height ();
            bool mouse_on_buttons = (//m_view_area->hasMouse () &&
                    mouse_pos.y () >= vert_buttons_pos-cp_height &&
                    mouse_pos.y ()<= vert_buttons_pos &&
                    mouse_pos.x () > 0 &&
                    mouse_pos.x () < m_control_panel->width());
            if (mouse_on_buttons && !m_control_panel->isVisible ()) {
                m_control_panel->show ();
                m_view_area->resizeEvent (0L);
            } else if (!mouse_on_buttons && m_control_panel->isVisible ()) {
                m_control_panel->hide ();
                m_view_area->resizeEvent (0L);
            }
        }
    } else if (e->timerId () == infopanel_timer) {
        if (m_infopanel->document ()->isEmpty ())
            m_dock_infopanel->hide ();
            //m_dock_infopanel->undock ();
        infopanel_timer  = 0;
    }
    killTimer (e->timerId ());
}

void View::addText (const QString & str, bool eol) {
    if (m_tmplog_needs_eol)
        tmplog += QChar ('\n');
    tmplog += str;
    m_tmplog_needs_eol = eol;
    if (!m_multiedit->isVisible () && tmplog.size () < 7500)
        return;
    if (eol) {
        if (m_multiedit->document ()->isEmpty ())
            m_multiedit->setPlainText (tmplog);
        else
            m_multiedit->append (tmplog);
        tmplog.truncate (0);
        m_tmplog_needs_eol = false;
    } else {
        int pos = tmplog.lastIndexOf (QChar ('\n'));
        if (pos >= 0) {
            m_multiedit->append (tmplog.left (pos));
            tmplog = tmplog.mid (pos+1);
        }
    }
    QTextCursor cursor = m_multiedit->textCursor ();
    cursor.movePosition (QTextCursor::End);
    cursor.movePosition (QTextCursor::PreviousBlock, QTextCursor::MoveAnchor, 5000);
    cursor.movePosition (QTextCursor::Start, QTextCursor::KeepAnchor);
    cursor.removeSelectedText ();
    cursor.movePosition (QTextCursor::End);
    m_multiedit->setTextCursor (cursor);
}

KDE_NO_EXPORT void View::videoStart () {
    if (!isFullScreen () && m_dockarea->centralWidget () != m_view_area) {
        // restore from an info or playlist only setting
        if (m_dockarea->centralWidget () == m_playlist)
            m_dock_playlist->setWidget (m_playlist);
        else if (m_dockarea->centralWidget () == m_infopanel)
            m_dock_infopanel->setWidget (m_infopanel);
        else
            m_status_bar->setVisible (false);
        m_dockarea->setCentralWidget (m_view_area);
    }
    if (m_controlpanel_mode == CP_Only) {
        m_control_panel->setMaximumSize(2500, controlPanel()->preferedHeight());
        setControlPanelMode (CP_Show);
    }
}

KDE_NO_EXPORT void View::playingStart () {
    if (m_playing)
        return; //FIXME: make symetric with playingStop
    m_playing = true;
    m_revert_fullscreen = !isFullScreen();
    setControlPanelMode (m_old_controlpanel_mode);
}

KDE_NO_EXPORT void View::playingStop () {
    if (m_controlpanel_mode == CP_AutoHide && m_image.isNull ()) {
        m_control_panel->show ();
        //m_view_area->setMouseTracking (false);
    }
    killTimer (controlbar_timer);
    controlbar_timer = 0;
    m_playing = false;
    m_view_area->resizeEvent (0L);
}

KDE_NO_EXPORT void View::leaveEvent (QEvent *) {
    delayedShowButtons (false);
}

KDE_NO_EXPORT void View::reset () {
    if (m_revert_fullscreen && isFullScreen ())
        m_control_panel->fullscreenAction->activate (QAction::Trigger);
        //m_view_area->fullScreen ();
    playingStop ();
}

bool View::isFullScreen () const {
    return m_view_area->isFullScreen ();
}

void View::fullScreen () {
    if (!m_view_area->isFullScreen()) {
        m_sreensaver_disabled = false;
        m_powerManagerStopSleep = Solid::PowerManagement::beginSuppressingSleep("KMplayer: watching a film");
        /*QByteArray data, replydata;
        QCString replyType;
        if (kapp->dcopClient ()->call ("kdesktop", "KScreensaverIface",
                    "isEnabled()", data, replyType, replydata)) {
            bool enabled;
            QDataStream replystream (replydata, IO_ReadOnly);
            replystream >> enabled;
            if (enabled)
                m_sreensaver_disabled = kapp->dcopClient()->send
                    ("kdesktop", "KScreensaverIface", "enable(bool)", "false");
        }*/
        //if (m_keepsizeratio && m_viewer->aspect () < 0.01)
        //    m_viewer->setAspect (1.0 * m_viewer->width() / m_viewer->height());
        m_view_area->fullScreen();
        m_control_panel->zoomAction->setVisible (false);
        //if (m_viewer->isVisible ())
        //    m_viewer->setFocus ();
    } else {
        Solid::PowerManagement::stopSuppressingSleep(m_powerManagerStopSleep);
       // if (m_sreensaver_disabled)
       //     m_sreensaver_disabled = !kapp->dcopClient()->send
       //         ("kdesktop", "KScreensaverIface", "enable(bool)", "true");
        m_view_area->fullScreen();
        m_control_panel->zoomAction->setVisible (true);
    }
    setControlPanelMode (m_old_controlpanel_mode);
    emit fullScreenChanged ();
}

KDE_NO_EXPORT int View::statusBarHeight () const {
    if (statusBar()->isVisible () && !viewArea()->isFullScreen ()) {
        if (statusBarMode () == SB_Only)
            return height ();
        else
            return statusBar()->maximumSize ().height ();
    }
    return 0;
}

#include "kmplayerview.moc"
