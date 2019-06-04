/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QEGLFSOPENWFDINTEGRATION_H
#define QEGLFSOPENWFDINTEGRATION_H

#include "private/qeglfsdeviceintegration_p.h"
#define WFD_WFDEXT_PROTOTYPES
#include "wfd.h"
#include "wfdext2.h"

QT_BEGIN_NAMESPACE

#define MAX_NUM_OF_WFD_BUFFERS 3
#define MAX_NUM_OF_WFD_DEVICES 4
#define MAX_NUM_OF_WFD_PIPELINES 16
#define MAX_NUM_OF_WFD_PORT_MODES 64
#define MAX_NUM_OF_WFD_PORTS 4

class QEglFSOpenWFDIntegration : public QEglFSDeviceIntegration
{
public:
    void platformInit() override;
    QSize screenSize() const override;
    EGLNativeWindowType createNativeWindow(QPlatformWindow *window, const QSize &size, const QSurfaceFormat &format) override;
    void destroyNativeWindow(EGLNativeWindowType window) override;
    EGLNativeDisplayType platformDisplay() const override;
    virtual QSurfaceFormat surfaceFormatFor(const QSurfaceFormat &inputFormat) const;

private:
    QSize mScreenSize;
    EGLNativeDisplayType mNativeDisplay;
    WFDDevice mDevice;
    WFDPort mPort;
    WFDPipeline mPipeline;
    WFDSource mSources[MAX_NUM_OF_WFD_BUFFERS] = {WFD_INVALID_HANDLE, WFD_INVALID_HANDLE, WFD_INVALID_HANDLE};
    WFD_EGLImageType* mWFDEglImages[MAX_NUM_OF_WFD_BUFFERS];
    WFDEGLImage mEGLImageHandles[MAX_NUM_OF_WFD_BUFFERS];
};

QT_END_NAMESPACE

#endif
