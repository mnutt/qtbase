/****************************************************************************
**
** Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL21$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia. For licensing terms and
** conditions see http://qt.digia.com/licensing. For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 or version 3 as published by the Free
** Software Foundation and appearing in the file LICENSE.LGPLv21 and
** LICENSE.LGPLv3 included in the packaging of this file. Please review the
** following information to ensure the GNU Lesser General Public License
** requirements will be met: https://www.gnu.org/licenses/lgpl.html and
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights. These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qeglfscontext.h"
#include "qeglfswindow.h"
#include "qeglfshooks.h"

#include <QtPlatformSupport/private/qeglconvenience_p.h>
#include <QtPlatformSupport/private/qeglpbuffer_p.h>
#include <QtPlatformSupport/private/qeglplatformcursor_p.h>
#include <QtGui/QSurface>
#include <QtDebug>

QT_BEGIN_NAMESPACE

QEglFSContext::QEglFSContext(const QSurfaceFormat &format, QPlatformOpenGLContext *share, EGLDisplay display,
                             EGLConfig *config, const QVariant &nativeHandle)
    : QEGLPlatformContext(format, share, display, config, nativeHandle),
      m_tempWindow(0)
{
}

EGLSurface QEglFSContext::eglSurfaceForPlatformSurface(QPlatformSurface *surface)
{
    if (surface->surface()->surfaceClass() == QSurface::Window)
        return static_cast<QEglFSWindow *>(surface)->surface();
    else
        return static_cast<QEGLPbuffer *>(surface)->pbuffer();
}

EGLSurface QEglFSContext::createTemporaryOffscreenSurface()
{
    if (QEglFSHooks::hooks()->supportsPBuffers())
        return QEGLPlatformContext::createTemporaryOffscreenSurface();

    if (!m_tempWindow) {
        m_tempWindow = QEglFSHooks::hooks()->createNativeOffscreenWindow(format());
        if (!m_tempWindow) {
            qWarning("QEglFSContext: Failed to create temporary native window");
            return EGL_NO_SURFACE;
        }
    }
    EGLConfig config = q_configFromGLFormat(eglDisplay(), format());
    return eglCreateWindowSurface(eglDisplay(), config, m_tempWindow, 0);
}

void QEglFSContext::destroyTemporaryOffscreenSurface(EGLSurface surface)
{
    if (QEglFSHooks::hooks()->supportsPBuffers()) {
        QEGLPlatformContext::destroyTemporaryOffscreenSurface(surface);
    } else {
        eglDestroySurface(eglDisplay(), surface);
        QEglFSHooks::hooks()->destroyNativeWindow(m_tempWindow);
        m_tempWindow = 0;
    }
}

void QEglFSContext::swapBuffers(QPlatformSurface *surface)
{
    // draw the cursor
    if (surface->surface()->surfaceClass() == QSurface::Window) {
        QPlatformWindow *window = static_cast<QPlatformWindow *>(surface);
        if (QEGLPlatformCursor *cursor = qobject_cast<QEGLPlatformCursor *>(window->screen()->cursor()))
            cursor->paintOnScreen();
    }

    QEglFSHooks::hooks()->waitForVSync(surface);
    QEGLPlatformContext::swapBuffers(surface);
    QEglFSHooks::hooks()->presentBuffer(surface);
}

QT_END_NAMESPACE
