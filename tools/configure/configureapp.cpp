/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the tools applications of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "configureapp.h"
#include "environment.h"
#ifdef COMMERCIAL_VERSION
#  include "tools.h"
#endif

#include <qdatetime.h>
#include <qdir.h>
#include <qdiriterator.h>
#include <qtemporaryfile.h>
#include <qstack.h>
#include <qdebug.h>
#include <qfileinfo.h>
#include <qtextstream.h>
#include <qregexp.h>
#include <qhash.h>

#include <iostream>
#include <string>
#include <fstream>
#include <windows.h>
#include <conio.h>

QT_BEGIN_NAMESPACE

enum Platforms {
    WINDOWS,
    WINDOWS_CE,
    QNX,
    BLACKBERRY
};

std::ostream &operator<<(std::ostream &s, const QString &val) {
    s << val.toLocal8Bit().data();
    return s;
}


using namespace std;

// Macros to simplify options marking
#define MARK_OPTION(x,y) ( dictionary[ #x ] == #y ? "*" : " " )


bool writeToFile(const char* text, const QString &filename)
{
    QByteArray symFile(text);
    QFile file(filename);
    QDir dir(QFileInfo(file).absoluteDir());
    if (!dir.exists())
        dir.mkpath(dir.absolutePath());
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        cout << "Couldn't write to " << qPrintable(filename) << ": " << qPrintable(file.errorString())
             << endl;
        return false;
    }
    file.write(symFile);
    return true;
}

Configure::Configure(int& argc, char** argv)
{
    // Default values for indentation
    optionIndent = 4;
    descIndent   = 25;
    outputWidth  = 0;
    // Get console buffer output width
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hStdout, &info))
        outputWidth = info.dwSize.X - 1;
    outputWidth = qMin(outputWidth, 79); // Anything wider gets unreadable
    if (outputWidth < 35) // Insanely small, just use 79
        outputWidth = 79;
    int i;

    /*
    ** Set up the initial state, the default
    */
    dictionary[ "CONFIGCMD" ] = argv[ 0 ];

    for (i = 1; i < argc; i++)
        configCmdLine += argv[ i ];

    if (configCmdLine.size() >= 2 && configCmdLine.at(0) == "-srcdir") {
        sourcePath = QDir::cleanPath(configCmdLine.at(1));
        sourceDir = QDir(sourcePath);
        configCmdLine.erase(configCmdLine.begin(), configCmdLine.begin() + 2);
    } else {
        // Get the path to the executable
        wchar_t module_name[MAX_PATH];
        GetModuleFileName(0, module_name, sizeof(module_name) / sizeof(wchar_t));
        QFileInfo sourcePathInfo = QString::fromWCharArray(module_name);
        sourcePath = sourcePathInfo.absolutePath();
        sourceDir = sourcePathInfo.dir();
    }
    buildPath = QDir::currentPath();
#if 0
    const QString installPath = QString("C:\\Qt\\%1").arg(QT_VERSION_STR);
#else
    const QString installPath = buildPath;
#endif
    if (sourceDir != buildDir) { //shadow builds!
        if (!findFile("perl") && !findFile("perl.exe")) {
            cout << "Error: Creating a shadow build of Qt requires" << endl
                 << "perl to be in the PATH environment";
            exit(0); // Exit cleanly for Ctrl+C
        }

        cout << "Preparing build tree..." << endl;
        QDir(buildPath).mkpath("bin");

        { //make a syncqt script(s) that can be used in the shadow
            QFile syncqt(buildPath + "/bin/syncqt");
            // no QFile::Text, just in case the perl interpreter can't cope with them (unlikely)
            if (syncqt.open(QFile::WriteOnly)) {
                QTextStream stream(&syncqt);
                stream << "#!/usr/bin/perl -w" << endl
                       << "require \"" << sourcePath + "/bin/syncqt\";" << endl;
            }
            QFile syncqt_bat(buildPath + "/bin/syncqt.bat");
            if (syncqt_bat.open(QFile::WriteOnly | QFile::Text)) {
                QTextStream stream(&syncqt_bat);
                stream << "@echo off" << endl
                       << "call " << QDir::toNativeSeparators(sourcePath + "/bin/syncqt.bat")
                       << " -qtdir \"" << QDir::toNativeSeparators(buildPath) << "\" %*" << endl;
                syncqt_bat.close();
            }
        }

        //copy the mkspecs
        buildDir.mkpath("mkspecs");
        if (!Environment::cpdir(sourcePath + "/mkspecs", buildPath + "/mkspecs")){
            cout << "Couldn't copy mkspecs!" << sourcePath << " " << buildPath << endl;
            dictionary["DONE"] = "error";
            return;
        }
    }

    defaultBuildParts << QStringLiteral("libs") << QStringLiteral("tools") << QStringLiteral("examples");
    dictionary[ "QT_SOURCE_TREE" ]    = sourcePath;
    dictionary[ "QT_BUILD_TREE" ]     = buildPath;
    dictionary[ "QT_INSTALL_PREFIX" ] = installPath;

    dictionary[ "QMAKESPEC" ] = getenv("QMAKESPEC");
    if (dictionary[ "QMAKESPEC" ].size() == 0) {
        dictionary[ "QMAKESPEC" ] = Environment::detectQMakeSpec();
        dictionary[ "QMAKESPEC_FROM" ] = "detected";
    } else {
        dictionary[ "QMAKESPEC_FROM" ] = "env";
    }

    dictionary[ "QCONFIG" ]         = "full";
    dictionary[ "EMBEDDED" ]        = "no";
    dictionary[ "BUILD_QMAKE" ]     = "yes";
    dictionary[ "VCPROJFILES" ]     = "yes";
    dictionary[ "QMAKE_INTERNAL" ]  = "no";
    dictionary[ "FAST" ]            = "no";
    dictionary[ "PROCESS" ]         = "partial";
    dictionary[ "WIDGETS" ]         = "yes";
    dictionary[ "RTTI" ]            = "yes";
    dictionary[ "SSE2" ]            = "auto";
    dictionary[ "SSE3" ]            = "auto";
    dictionary[ "SSSE3" ]           = "auto";
    dictionary[ "SSE4_1" ]          = "auto";
    dictionary[ "SSE4_2" ]          = "auto";
    dictionary[ "AVX" ]             = "auto";
    dictionary[ "AVX2" ]            = "auto";
    dictionary[ "IWMMXT" ]          = "auto";
    dictionary[ "SYNCQT" ]          = "auto";
    dictionary[ "CE_CRT" ]          = "no";
    dictionary[ "CETEST" ]          = "auto";
    dictionary[ "CE_SIGNATURE" ]    = "no";
    dictionary[ "AUDIO_BACKEND" ]   = "auto";
    dictionary[ "WMSDK" ]           = "auto";
    dictionary[ "V8SNAPSHOT" ]      = "auto";
    dictionary[ "QML_DEBUG" ]       = "yes";
    dictionary[ "PLUGIN_MANIFESTS" ] = "yes";
    dictionary[ "DIRECTWRITE" ]     = "no";
    dictionary[ "NIS" ]             = "no";
    dictionary[ "NEON" ]            = "no";
    dictionary[ "LARGE_FILE" ]      = "yes";
    dictionary[ "FONT_CONFIG" ]     = "no";
    dictionary[ "POSIX_IPC" ]       = "no";
    dictionary[ "QT_GLIB" ]         = "no";
    dictionary[ "QT_ICONV" ]        = "auto";
    dictionary[ "QT_CUPS" ]         = "auto";
    dictionary[ "CFG_GCC_SYSROOT" ] = "yes";

    //Only used when cross compiling.
    dictionary[ "QT_INSTALL_SETTINGS" ] = "/etc/xdg";

    QString version;
    QFile qglobal_h(sourcePath + "/src/corelib/global/qglobal.h");
    if (qglobal_h.open(QFile::ReadOnly)) {
        QTextStream read(&qglobal_h);
        QRegExp version_regexp("^# *define *QT_VERSION_STR *\"([^\"]*)\"");
        QString line;
        while (!read.atEnd()) {
            line = read.readLine();
            if (version_regexp.exactMatch(line)) {
                version = version_regexp.cap(1).trimmed();
                if (!version.isEmpty())
                    break;
            }
        }
        qglobal_h.close();
    }

    if (version.isEmpty())
        version = QString("%1.%2.%3").arg(QT_VERSION>>16).arg(((QT_VERSION>>8)&0xff)).arg(QT_VERSION&0xff);

    dictionary[ "VERSION" ]         = version;
    {
        QRegExp version_re("([0-9]*)\\.([0-9]*)\\.([0-9]*)(|-.*)");
        if (version_re.exactMatch(version)) {
            dictionary[ "VERSION_MAJOR" ] = version_re.cap(1);
            dictionary[ "VERSION_MINOR" ] = version_re.cap(2);
            dictionary[ "VERSION_PATCH" ] = version_re.cap(3);
        }
    }

    dictionary[ "REDO" ]            = "no";
    dictionary[ "DEPENDENCIES" ]    = "no";

    dictionary[ "BUILD" ]           = "debug";
    dictionary[ "BUILDALL" ]        = "auto"; // Means yes, but not explicitly
    dictionary[ "FORCEDEBUGINFO" ]  = "no";

    dictionary[ "BUILDTYPE" ]      = "none";

    dictionary[ "BUILDDEV" ]        = "no";

    dictionary[ "SHARED" ]          = "yes";

    dictionary[ "ZLIB" ]            = "auto";

    dictionary[ "PCRE" ]            = "auto";

    dictionary[ "ICU" ]             = "auto";

    dictionary[ "GIF" ]             = "auto";
    dictionary[ "JPEG" ]            = "auto";
    dictionary[ "PNG" ]             = "auto";
    dictionary[ "LIBJPEG" ]         = "auto";
    dictionary[ "LIBPNG" ]          = "auto";
    dictionary[ "FREETYPE" ]        = "yes";

    dictionary[ "ACCESSIBILITY" ]   = "yes";
    dictionary[ "OPENGL" ]          = "yes";
    dictionary[ "OPENVG" ]          = "no";
    dictionary[ "OPENSSL" ]         = "auto";
    dictionary[ "DBUS" ]            = "auto";

    dictionary[ "STYLE_WINDOWS" ]   = "yes";
    dictionary[ "STYLE_WINDOWSXP" ] = "auto";
    dictionary[ "STYLE_WINDOWSVISTA" ] = "auto";
    dictionary[ "STYLE_PLASTIQUE" ] = "yes";
    dictionary[ "STYLE_CLEANLOOKS" ]= "yes";
    dictionary[ "STYLE_WINDOWSCE" ] = "no";
    dictionary[ "STYLE_WINDOWSMOBILE" ] = "no";
    dictionary[ "STYLE_MOTIF" ]     = "yes";
    dictionary[ "STYLE_CDE" ]       = "yes";
    dictionary[ "STYLE_GTK" ]       = "no";

    dictionary[ "SQL_MYSQL" ]       = "no";
    dictionary[ "SQL_ODBC" ]        = "no";
    dictionary[ "SQL_OCI" ]         = "no";
    dictionary[ "SQL_PSQL" ]        = "no";
    dictionary[ "SQL_TDS" ]         = "no";
    dictionary[ "SQL_DB2" ]         = "no";
    dictionary[ "SQL_SQLITE" ]      = "auto";
    dictionary[ "SQL_SQLITE_LIB" ]  = "qt";
    dictionary[ "SQL_SQLITE2" ]     = "no";
    dictionary[ "SQL_IBASE" ]       = "no";

    QString tmp = dictionary[ "QMAKESPEC" ];
    if (tmp.contains("\\")) {
        tmp = tmp.mid(tmp.lastIndexOf("\\") + 1);
    } else {
        tmp = tmp.mid(tmp.lastIndexOf("/") + 1);
    }
    dictionary[ "QMAKESPEC" ] = tmp;

    dictionary[ "INCREDIBUILD_XGE" ] = "auto";
    dictionary[ "LTCG" ]            = "no";
    dictionary[ "NATIVE_GESTURES" ] = "yes";
    dictionary[ "MSVC_MP" ] = "no";
}

Configure::~Configure()
{
    for (int i=0; i<3; ++i) {
        QList<MakeItem*> items = makeList[i];
        for (int j=0; j<items.size(); ++j)
            delete items[j];
    }
}

QString Configure::formatPath(const QString &path)
{
    QString ret = QDir::cleanPath(path);
    // This amount of quoting is deemed sufficient.
    if (ret.contains(QLatin1Char(' '))) {
        ret.prepend(QLatin1Char('"'));
        ret.append(QLatin1Char('"'));
    }
    return ret;
}

QString Configure::formatPaths(const QStringList &paths)
{
    QString ret;
    foreach (const QString &path, paths) {
        if (!ret.isEmpty())
            ret += QLatin1Char(' ');
        ret += formatPath(path);
    }
    return ret;
}

// We could use QDir::homePath() + "/.qt-license", but
// that will only look in the first of $HOME,$USERPROFILE
// or $HOMEDRIVE$HOMEPATH. So, here we try'em all to be
// more forgiving for the end user..
QString Configure::firstLicensePath()
{
    QStringList allPaths;
    allPaths << "./.qt-license"
             << QString::fromLocal8Bit(getenv("HOME")) + "/.qt-license"
             << QString::fromLocal8Bit(getenv("USERPROFILE")) + "/.qt-license"
             << QString::fromLocal8Bit(getenv("HOMEDRIVE")) + QString::fromLocal8Bit(getenv("HOMEPATH")) + "/.qt-license";
    for (int i = 0; i< allPaths.count(); ++i)
        if (QFile::exists(allPaths.at(i)))
            return allPaths.at(i);
    return QString();
}

// #### somehow I get a compiler error about vc++ reaching the nesting limit without
// undefining the ansi for scoping.
#ifdef for
#undef for
#endif

void Configure::parseCmdLine()
{
    int argCount = configCmdLine.size();
    int i = 0;
    const QStringList imageFormats = QStringList() << "gif" << "png" << "jpeg";

#if !defined(EVAL)
    if (argCount < 1) // skip rest if no arguments
        ;
    else if (configCmdLine.at(i) == "-redo") {
        dictionary[ "REDO" ] = "yes";
        configCmdLine.clear();
        reloadCmdLine();
    }
    else if (configCmdLine.at(i) == "-loadconfig") {
        ++i;
        if (i != argCount) {
            dictionary[ "REDO" ] = "yes";
            dictionary[ "CUSTOMCONFIG" ] = "_" + configCmdLine.at(i);
            configCmdLine.clear();
            reloadCmdLine();
        } else {
            dictionary[ "HELP" ] = "yes";
        }
        i = 0;
    }
    argCount = configCmdLine.size();
#endif

    // Look first for XQMAKESPEC
    for (int j = 0 ; j < argCount; ++j)
    {
        if (configCmdLine.at(j) == "-xplatform") {
            ++j;
            if (j == argCount)
                break;
            dictionary["XQMAKESPEC"] = configCmdLine.at(j);
            if (!dictionary[ "XQMAKESPEC" ].isEmpty())
                applySpecSpecifics();
        }
    }

    for (; i<configCmdLine.size(); ++i) {
        bool continueElse[] = {false, false};
        if (configCmdLine.at(i) == "-help"
            || configCmdLine.at(i) == "-h"
            || configCmdLine.at(i) == "-?")
            dictionary[ "HELP" ] = "yes";

#if !defined(EVAL)
        else if (configCmdLine.at(i) == "-qconfig") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QCONFIG" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-release") {
            dictionary[ "BUILD" ] = "release";
            if (dictionary[ "BUILDALL" ] == "auto")
                dictionary[ "BUILDALL" ] = "no";
        } else if (configCmdLine.at(i) == "-debug") {
            dictionary[ "BUILD" ] = "debug";
            if (dictionary[ "BUILDALL" ] == "auto")
                dictionary[ "BUILDALL" ] = "no";
        } else if (configCmdLine.at(i) == "-debug-and-release")
            dictionary[ "BUILDALL" ] = "yes";
        else if (configCmdLine.at(i) == "-force-debug-info")
            dictionary[ "FORCEDEBUGINFO" ] = "yes";

        else if (configCmdLine.at(i) == "-shared")
            dictionary[ "SHARED" ] = "yes";
        else if (configCmdLine.at(i) == "-static")
            dictionary[ "SHARED" ] = "no";
        else if (configCmdLine.at(i) == "-developer-build")
            dictionary[ "BUILDDEV" ] = "yes";
        else if (configCmdLine.at(i) == "-opensource") {
            dictionary[ "BUILDTYPE" ] = "opensource";
        }
        else if (configCmdLine.at(i) == "-commercial") {
            dictionary[ "BUILDTYPE" ] = "commercial";
        }
        else if (configCmdLine.at(i) == "-ltcg") {
            dictionary[ "LTCG" ] = "yes";
        }
        else if (configCmdLine.at(i) == "-no-ltcg") {
            dictionary[ "LTCG" ] = "no";
        }
        else if (configCmdLine.at(i) == "-mp") {
            dictionary[ "MSVC_MP" ] = "yes";
        }
        else if (configCmdLine.at(i) == "-no-mp") {
            dictionary[ "MSVC_MP" ] = "no";
        }
        else if (configCmdLine.at(i) == "-force-asserts") {
            dictionary[ "FORCE_ASSERTS" ] = "yes";
        }


#endif

        else if (configCmdLine.at(i) == "-platform") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QMAKESPEC" ] = configCmdLine.at(i);
        dictionary[ "QMAKESPEC_FROM" ] = "commandline";
        } else if (configCmdLine.at(i) == "-arch") {
            ++i;
            if (i == argCount)
                break;
            dictionary["OBSOLETE_ARCH_ARG"] = "yes";
        } else if (configCmdLine.at(i) == "-embedded") {
            dictionary[ "EMBEDDED" ] = "yes";
        } else if (configCmdLine.at(i) == "-xplatform") {
            ++i;
            // do nothing
        }


#if !defined(EVAL)
        else if (configCmdLine.at(i) == "-no-zlib") {
            // No longer supported since Qt 4.4.0
            // But save the information for later so that we can print a warning
            //
            // If you REALLY really need no zlib support, you can still disable
            // it by doing the following:
            //   add "no-zlib" to mkspecs/qconfig.pri
            //   #define QT_NO_COMPRESS (probably by adding to src/corelib/global/qconfig.h)
            //
            // There's no guarantee that Qt will build under those conditions

            dictionary[ "ZLIB_FORCED" ] = "yes";
        } else if (configCmdLine.at(i) == "-qt-zlib") {
            dictionary[ "ZLIB" ] = "qt";
        } else if (configCmdLine.at(i) == "-system-zlib") {
            dictionary[ "ZLIB" ] = "system";
        }

        else if (configCmdLine.at(i) == "-qt-pcre") {
            dictionary[ "PCRE" ] = "qt";
        } else if (configCmdLine.at(i) == "-system-pcre") {
            dictionary[ "PCRE" ] = "system";
        }

        else if (configCmdLine.at(i) == "-icu") {
            dictionary[ "ICU" ] = "yes";
        } else if (configCmdLine.at(i) == "-no-icu") {
            dictionary[ "ICU" ] = "no";
        }

        // Image formats --------------------------------------------
        else if (configCmdLine.at(i) == "-no-gif")
            dictionary[ "GIF" ] = "no";

        else if (configCmdLine.at(i) == "-no-libjpeg") {
            dictionary[ "JPEG" ] = "no";
            dictionary[ "LIBJPEG" ] = "no";
        } else if (configCmdLine.at(i) == "-qt-libjpeg") {
            dictionary[ "LIBJPEG" ] = "qt";
        } else if (configCmdLine.at(i) == "-system-libjpeg") {
            dictionary[ "LIBJPEG" ] = "system";
        }

        else if (configCmdLine.at(i) == "-no-libpng") {
            dictionary[ "PNG" ] = "no";
            dictionary[ "LIBPNG" ] = "no";
        } else if (configCmdLine.at(i) == "-qt-libpng") {
            dictionary[ "LIBPNG" ] = "qt";
        } else if (configCmdLine.at(i) == "-system-libpng") {
            dictionary[ "LIBPNG" ] = "system";
        }

        // Text Rendering --------------------------------------------
        else if (configCmdLine.at(i) == "-no-freetype")
            dictionary[ "FREETYPE" ] = "no";
        else if (configCmdLine.at(i) == "-qt-freetype")
            dictionary[ "FREETYPE" ] = "yes";
        else if (configCmdLine.at(i) == "-system-freetype")
            dictionary[ "FREETYPE" ] = "system";

        // CE- C runtime --------------------------------------------
        else if (configCmdLine.at(i) == "-crt") {
            ++i;
            if (i == argCount)
                break;
            QDir cDir(configCmdLine.at(i));
            if (!cDir.exists())
                cout << "WARNING: Could not find directory (" << qPrintable(configCmdLine.at(i)) << ")for C runtime deployment" << endl;
            else
                dictionary[ "CE_CRT" ] = QDir::toNativeSeparators(cDir.absolutePath());
        } else if (configCmdLine.at(i) == "-qt-crt") {
            dictionary[ "CE_CRT" ] = "yes";
        } else if (configCmdLine.at(i) == "-no-crt") {
            dictionary[ "CE_CRT" ] = "no";
        }
        // cetest ---------------------------------------------------
        else if (configCmdLine.at(i) == "-no-cetest") {
            dictionary[ "CETEST" ] = "no";
            dictionary[ "CETEST_REQUESTED" ] = "no";
        } else if (configCmdLine.at(i) == "-cetest") {
            // although specified to use it, we stay at "auto" state
            // this is because checkAvailability() adds variables
            // we need for crosscompilation; but remember if we asked
            // for it.
            dictionary[ "CETEST_REQUESTED" ] = "yes";
        }
        // Qt/CE - signing tool -------------------------------------
        else if (configCmdLine.at(i) == "-signature") {
            ++i;
            if (i == argCount)
                break;
            QFileInfo info(configCmdLine.at(i));
            if (!info.exists())
                cout << "WARNING: Could not find signature file (" << qPrintable(configCmdLine.at(i)) << ")" << endl;
            else
                dictionary[ "CE_SIGNATURE" ] = QDir::toNativeSeparators(info.absoluteFilePath());
        }
        // Styles ---------------------------------------------------
        else if (configCmdLine.at(i) == "-qt-style-windows")
            dictionary[ "STYLE_WINDOWS" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-windows")
            dictionary[ "STYLE_WINDOWS" ] = "no";

        else if (configCmdLine.at(i) == "-qt-style-windowsce")
            dictionary[ "STYLE_WINDOWSCE" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-windowsce")
            dictionary[ "STYLE_WINDOWSCE" ] = "no";
        else if (configCmdLine.at(i) == "-qt-style-windowsmobile")
            dictionary[ "STYLE_WINDOWSMOBILE" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-windowsmobile")
            dictionary[ "STYLE_WINDOWSMOBILE" ] = "no";

        else if (configCmdLine.at(i) == "-qt-style-windowsxp")
            dictionary[ "STYLE_WINDOWSXP" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-windowsxp")
            dictionary[ "STYLE_WINDOWSXP" ] = "no";

        else if (configCmdLine.at(i) == "-qt-style-windowsvista")
            dictionary[ "STYLE_WINDOWSVISTA" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-windowsvista")
            dictionary[ "STYLE_WINDOWSVISTA" ] = "no";

        else if (configCmdLine.at(i) == "-qt-style-plastique")
            dictionary[ "STYLE_PLASTIQUE" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-plastique")
            dictionary[ "STYLE_PLASTIQUE" ] = "no";

        else if (configCmdLine.at(i) == "-qt-style-cleanlooks")
            dictionary[ "STYLE_CLEANLOOKS" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-cleanlooks")
            dictionary[ "STYLE_CLEANLOOKS" ] = "no";

        else if (configCmdLine.at(i) == "-qt-style-motif")
            dictionary[ "STYLE_MOTIF" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-motif")
            dictionary[ "STYLE_MOTIF" ] = "no";

        else if (configCmdLine.at(i) == "-qt-style-cde")
            dictionary[ "STYLE_CDE" ] = "yes";
        else if (configCmdLine.at(i) == "-no-style-cde")
            dictionary[ "STYLE_CDE" ] = "no";

        // Work around compiler nesting limitation
        else
            continueElse[1] = true;
        if (!continueElse[1]) {
        }

        // OpenGL Support -------------------------------------------
        else if (configCmdLine.at(i) == "-no-opengl") {
            dictionary[ "OPENGL" ]    = "no";
        } else if (configCmdLine.at(i) == "-opengl-es-cm") {
            dictionary[ "OPENGL" ]          = "yes";
            dictionary[ "OPENGL_ES_CM" ]    = "yes";
        } else if (configCmdLine.at(i) == "-opengl-es-2") {
            dictionary[ "OPENGL" ]          = "yes";
            dictionary[ "OPENGL_ES_2" ]     = "yes";
        } else if (configCmdLine.at(i) == "-opengl") {
            dictionary[ "OPENGL" ]          = "yes";
            i++;
            if (i == argCount)
                break;

            if (configCmdLine.at(i) == "es1") {
                dictionary[ "OPENGL_ES_CM" ]    = "yes";
            } else if ( configCmdLine.at(i) == "es2" ) {
                dictionary[ "OPENGL_ES_2" ]     = "yes";
            } else if ( configCmdLine.at(i) == "desktop" ) {
                // OPENGL=yes suffices
            } else {
                cout << "Argument passed to -opengl option is not valid." << endl;
                dictionary[ "DONE" ] = "error";
                break;
            }
        // External location of ANGLE library  (Open GL ES 2)
        } else if (configCmdLine.at(i) == QStringLiteral("-angle")) {
            if (++i == argCount)
              break;
            const QFileInfo fi(configCmdLine.at(i));
            if (!fi.isDir()) {
                cout << "Argument passed to -angle option is not a directory." << endl;
                dictionary.insert(QStringLiteral("DONE"), QStringLiteral( "error"));
            }
            dictionary.insert(QStringLiteral("ANGLE_DIR"), fi.absoluteFilePath());
        }

        // OpenVG Support -------------------------------------------
        else if (configCmdLine.at(i) == "-openvg") {
            dictionary[ "OPENVG" ]    = "yes";
        } else if (configCmdLine.at(i) == "-no-openvg") {
            dictionary[ "OPENVG" ]    = "no";
        }

        // Databases ------------------------------------------------
        else if (configCmdLine.at(i) == "-qt-sql-mysql")
            dictionary[ "SQL_MYSQL" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-mysql")
            dictionary[ "SQL_MYSQL" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-mysql")
            dictionary[ "SQL_MYSQL" ] = "no";

        else if (configCmdLine.at(i) == "-qt-sql-odbc")
            dictionary[ "SQL_ODBC" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-odbc")
            dictionary[ "SQL_ODBC" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-odbc")
            dictionary[ "SQL_ODBC" ] = "no";

        else if (configCmdLine.at(i) == "-qt-sql-oci")
            dictionary[ "SQL_OCI" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-oci")
            dictionary[ "SQL_OCI" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-oci")
            dictionary[ "SQL_OCI" ] = "no";

        else if (configCmdLine.at(i) == "-qt-sql-psql")
            dictionary[ "SQL_PSQL" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-psql")
            dictionary[ "SQL_PSQL" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-psql")
            dictionary[ "SQL_PSQL" ] = "no";

        else if (configCmdLine.at(i) == "-qt-sql-tds")
            dictionary[ "SQL_TDS" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-tds")
            dictionary[ "SQL_TDS" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-tds")
            dictionary[ "SQL_TDS" ] = "no";

        else if (configCmdLine.at(i) == "-qt-sql-db2")
            dictionary[ "SQL_DB2" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-db2")
            dictionary[ "SQL_DB2" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-db2")
            dictionary[ "SQL_DB2" ] = "no";

        else if (configCmdLine.at(i) == "-qt-sql-sqlite")
            dictionary[ "SQL_SQLITE" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-sqlite")
            dictionary[ "SQL_SQLITE" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-sqlite")
            dictionary[ "SQL_SQLITE" ] = "no";
        else if (configCmdLine.at(i) == "-system-sqlite")
            dictionary[ "SQL_SQLITE_LIB" ] = "system";
        else if (configCmdLine.at(i) == "-qt-sql-sqlite2")
            dictionary[ "SQL_SQLITE2" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-sqlite2")
            dictionary[ "SQL_SQLITE2" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-sqlite2")
            dictionary[ "SQL_SQLITE2" ] = "no";

        else if (configCmdLine.at(i) == "-qt-sql-ibase")
            dictionary[ "SQL_IBASE" ] = "yes";
        else if (configCmdLine.at(i) == "-plugin-sql-ibase")
            dictionary[ "SQL_IBASE" ] = "plugin";
        else if (configCmdLine.at(i) == "-no-sql-ibase")
            dictionary[ "SQL_IBASE" ] = "no";

        // Image formats --------------------------------------------
        else if (configCmdLine.at(i).startsWith("-qt-imageformat-") &&
                 imageFormats.contains(configCmdLine.at(i).section('-', 3)))
            dictionary[ configCmdLine.at(i).section('-', 3).toUpper() ] = "yes";
        else if (configCmdLine.at(i).startsWith("-plugin-imageformat-") &&
                 imageFormats.contains(configCmdLine.at(i).section('-', 3)))
            dictionary[ configCmdLine.at(i).section('-', 3).toUpper() ] = "plugin";
        else if (configCmdLine.at(i).startsWith("-no-imageformat-") &&
                 imageFormats.contains(configCmdLine.at(i).section('-', 3)))
            dictionary[ configCmdLine.at(i).section('-', 3).toUpper() ] = "no";
#endif
        // IDE project generation -----------------------------------
        else if (configCmdLine.at(i) == "-no-vcproj")
            dictionary[ "VCPROJFILES" ] = "no";
        else if (configCmdLine.at(i) == "-vcproj")
            dictionary[ "VCPROJFILES" ] = "yes";

        else if (configCmdLine.at(i) == "-no-incredibuild-xge")
            dictionary[ "INCREDIBUILD_XGE" ] = "no";
        else if (configCmdLine.at(i) == "-incredibuild-xge")
            dictionary[ "INCREDIBUILD_XGE" ] = "yes";
        else if (configCmdLine.at(i) == "-native-gestures")
            dictionary[ "NATIVE_GESTURES" ] = "yes";
        else if (configCmdLine.at(i) == "-no-native-gestures")
            dictionary[ "NATIVE_GESTURES" ] = "no";
#if !defined(EVAL)
        // Others ---------------------------------------------------
        else if (configCmdLine.at(i) == "-fast")
            dictionary[ "FAST" ] = "yes";
        else if (configCmdLine.at(i) == "-no-fast")
            dictionary[ "FAST" ] = "no";

        else if (configCmdLine.at(i) == "-widgets")
            dictionary[ "WIDGETS" ] = "yes";
        else if (configCmdLine.at(i) == "-no-widgets")
            dictionary[ "WIDGETS" ] = "no";

        else if (configCmdLine.at(i) == "-rtti")
            dictionary[ "RTTI" ] = "yes";
        else if (configCmdLine.at(i) == "-no-rtti")
            dictionary[ "RTTI" ] = "no";

        else if (configCmdLine.at(i) == "-accessibility")
            dictionary[ "ACCESSIBILITY" ] = "yes";
        else if (configCmdLine.at(i) == "-no-accessibility") {
            dictionary[ "ACCESSIBILITY" ] = "no";
            cout << "Setting accessibility to NO" << endl;
        }

        else if (configCmdLine.at(i) == "-no-sse2")
            dictionary[ "SSE2" ] = "no";
        else if (configCmdLine.at(i) == "-sse2")
            dictionary[ "SSE2" ] = "yes";
        else if (configCmdLine.at(i) == "-no-sse3")
            dictionary[ "SSE3" ] = "no";
        else if (configCmdLine.at(i) == "-sse3")
            dictionary[ "SSE3" ] = "yes";
        else if (configCmdLine.at(i) == "-no-ssse3")
            dictionary[ "SSSE3" ] = "no";
        else if (configCmdLine.at(i) == "-ssse3")
            dictionary[ "SSSE3" ] = "yes";
        else if (configCmdLine.at(i) == "-no-sse4.1")
            dictionary[ "SSE4_1" ] = "no";
        else if (configCmdLine.at(i) == "-sse4.1")
            dictionary[ "SSE4_1" ] = "yes";
        else if (configCmdLine.at(i) == "-no-sse4.2")
            dictionary[ "SSE4_2" ] = "no";
        else if (configCmdLine.at(i) == "-sse4.2")
            dictionary[ "SSE4_2" ] = "yes";
        else if (configCmdLine.at(i) == "-no-avx")
            dictionary[ "AVX" ] = "no";
        else if (configCmdLine.at(i) == "-avx")
            dictionary[ "AVX" ] = "yes";
        else if (configCmdLine.at(i) == "-no-avx2")
            dictionary[ "AVX2" ] = "no";
        else if (configCmdLine.at(i) == "-avx2")
            dictionary[ "AVX2" ] = "yes";
        else if (configCmdLine.at(i) == "-no-iwmmxt")
            dictionary[ "IWMMXT" ] = "no";
        else if (configCmdLine.at(i) == "-iwmmxt")
            dictionary[ "IWMMXT" ] = "yes";

        else if (configCmdLine.at(i) == "-no-openssl") {
              dictionary[ "OPENSSL"] = "no";
        } else if (configCmdLine.at(i) == "-openssl") {
              dictionary[ "OPENSSL" ] = "yes";
        } else if (configCmdLine.at(i) == "-openssl-linked") {
              dictionary[ "OPENSSL" ] = "linked";
        } else if (configCmdLine.at(i) == "-no-qdbus") {
            dictionary[ "DBUS" ] = "no";
        } else if (configCmdLine.at(i) == "-qdbus") {
            dictionary[ "DBUS" ] = "yes";
        } else if (configCmdLine.at(i) == "-no-dbus") {
            dictionary[ "DBUS" ] = "no";
        } else if (configCmdLine.at(i) == "-dbus") {
            dictionary[ "DBUS" ] = "yes";
        } else if (configCmdLine.at(i) == "-dbus-linked") {
            dictionary[ "DBUS" ] = "linked";
        } else if (configCmdLine.at(i) == "-audio-backend") {
            dictionary[ "AUDIO_BACKEND" ] = "yes";
        } else if (configCmdLine.at(i) == "-no-audio-backend") {
            dictionary[ "AUDIO_BACKEND" ] = "no";
        } else if (configCmdLine.at(i) == "-no-qml-debug") {
            dictionary[ "QML_DEBUG" ] = "no";
        } else if (configCmdLine.at(i) == "-qml-debug") {
            dictionary[ "QML_DEBUG" ] = "yes";
        } else if (configCmdLine.at(i) == "-no-plugin-manifests") {
            dictionary[ "PLUGIN_MANIFESTS" ] = "no";
        } else if (configCmdLine.at(i) == "-plugin-manifests") {
            dictionary[ "PLUGIN_MANIFESTS" ] = "yes";
        }

        // Work around compiler nesting limitation
        else
            continueElse[0] = true;
        if (!continueElse[0]) {
        }

        else if (configCmdLine.at(i) == "-internal")
            dictionary[ "QMAKE_INTERNAL" ] = "yes";

        else if (configCmdLine.at(i) == "-no-syncqt")
            dictionary[ "SYNCQT" ] = "no";

        else if (configCmdLine.at(i) == "-no-qmake")
            dictionary[ "BUILD_QMAKE" ] = "no";
        else if (configCmdLine.at(i) == "-qmake")
            dictionary[ "BUILD_QMAKE" ] = "yes";

        else if (configCmdLine.at(i) == "-dont-process")
            dictionary[ "PROCESS" ] = "no";
        else if (configCmdLine.at(i) == "-process")
            dictionary[ "PROCESS" ] = "partial";
        else if (configCmdLine.at(i) == "-fully-process")
            dictionary[ "PROCESS" ] = "full";

        else if (configCmdLine.at(i) == "-no-qmake-deps")
            dictionary[ "DEPENDENCIES" ] = "no";
        else if (configCmdLine.at(i) == "-qmake-deps")
            dictionary[ "DEPENDENCIES" ] = "yes";


        else if (configCmdLine.at(i) == "-qtnamespace") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_NAMESPACE" ] = configCmdLine.at(i);
        } else if (configCmdLine.at(i) == "-qtlibinfix") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_LIBINFIX" ] = configCmdLine.at(i);
        } else if (configCmdLine.at(i) == "-D") {
            ++i;
            if (i == argCount)
                break;
            qmakeDefines += configCmdLine.at(i);
        } else if (configCmdLine.at(i) == "-I") {
            ++i;
            if (i == argCount)
                break;
            qmakeIncludes += configCmdLine.at(i);
        } else if (configCmdLine.at(i) == "-L") {
            ++i;
            if (i == argCount)
                break;
            QFileInfo checkDirectory(configCmdLine.at(i));
            if (!checkDirectory.isDir()) {
                cout << "Argument passed to -L option is not a directory path. Did you mean the -l option?" << endl;
                dictionary[ "DONE" ] = "error";
                break;
            }
            qmakeLibs += QString("-L" + configCmdLine.at(i));
        } else if (configCmdLine.at(i) == "-l") {
            ++i;
            if (i == argCount)
                break;
            qmakeLibs += QString("-l" + configCmdLine.at(i));
        } else if (configCmdLine.at(i).startsWith("OPENSSL_LIBS=")) {
            opensslLibs = configCmdLine.at(i);
        } else if (configCmdLine.at(i).startsWith("OPENSSL_LIBS_DEBUG=")) {
            opensslLibsDebug = configCmdLine.at(i);
        } else if (configCmdLine.at(i).startsWith("OPENSSL_LIBS_RELEASE=")) {
            opensslLibsRelease = configCmdLine.at(i);
        } else if (configCmdLine.at(i).startsWith("OPENSSL_PATH=")) {
            opensslPath = QDir::fromNativeSeparators(configCmdLine.at(i));
        } else if (configCmdLine.at(i).startsWith("PSQL_LIBS=")) {
            psqlLibs = configCmdLine.at(i);
        } else if (configCmdLine.at(i).startsWith("SYBASE=")) {
            sybase = configCmdLine.at(i);
        } else if (configCmdLine.at(i).startsWith("SYBASE_LIBS=")) {
            sybaseLibs = configCmdLine.at(i);
        } else if (configCmdLine.at(i).startsWith("DBUS_PATH=")) {
            dbusPath = QDir::fromNativeSeparators(configCmdLine.at(i));
        } else if (configCmdLine.at(i).startsWith("MYSQL_PATH=")) {
            mysqlPath = QDir::fromNativeSeparators(configCmdLine.at(i));
        } else if (configCmdLine.at(i).startsWith("ZLIB_LIBS=")) {
            zlibLibs = QDir::fromNativeSeparators(configCmdLine.at(i));
        }

        else if ((configCmdLine.at(i) == "-override-version") || (configCmdLine.at(i) == "-version-override")){
            ++i;
            if (i == argCount)
                break;
            dictionary[ "VERSION" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-saveconfig") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "CUSTOMCONFIG" ] = "_" + configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-confirm-license") {
            dictionary["LICENSE_CONFIRMED"] = "yes";
        }

        else if (configCmdLine.at(i) == "-make") {
            ++i;
            if (i == argCount)
                break;
            buildParts += configCmdLine.at(i);
        } else if (configCmdLine.at(i) == "-nomake") {
            ++i;
            if (i == argCount)
                break;
            nobuildParts.append(configCmdLine.at(i));
        }

        // Directories ----------------------------------------------
        else if (configCmdLine.at(i) == "-prefix") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_PREFIX" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-bindir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_BINS" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-libdir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_LIBS" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-docdir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_DOCS" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-headerdir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_HEADERS" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-plugindir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_PLUGINS" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-importdir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_IMPORTS" ] = configCmdLine.at(i);
        }
        else if (configCmdLine.at(i) == "-datadir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_DATA" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-translationdir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_TRANSLATIONS" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-examplesdir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_EXAMPLES" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-testsdir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_INSTALL_TESTS" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-sysroot") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "CFG_SYSROOT" ] = configCmdLine.at(i);
        }
        else if (configCmdLine.at(i) == "-no-gcc-sysroot") {
            dictionary[ "CFG_GCC_SYSROOT" ] = "no";
        }

        else if (configCmdLine.at(i) == "-hostprefix") {
            ++i;
            if (i == argCount || configCmdLine.at(i).startsWith('-'))
                dictionary[ "QT_HOST_PREFIX" ] = dictionary[ "QT_BUILD_TREE" ];
            else
                dictionary[ "QT_HOST_PREFIX" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-hostbindir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_HOST_BINS" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-hostdatadir") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "QT_HOST_DATA" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i) == "-make-tool") {
            ++i;
            if (i == argCount)
                break;
            dictionary[ "MAKE" ] = configCmdLine.at(i);
        }

        else if (configCmdLine.at(i).indexOf(QRegExp("^-(en|dis)able-")) != -1) {
            // Scan to see if any specific modules and drivers are enabled or disabled
            for (QStringList::Iterator module = modules.begin(); module != modules.end(); ++module) {
                if (configCmdLine.at(i) == QString("-enable-") + (*module)) {
                    enabledModules += (*module);
                    break;
                }
                else if (configCmdLine.at(i) == QString("-disable-") + (*module)) {
                    disabledModules += (*module);
                    break;
                }
            }
        }

        else if (configCmdLine.at(i) == "-directwrite") {
            dictionary["DIRECTWRITE"] = "yes";
        } else if (configCmdLine.at(i) == "-no-directwrite") {
            dictionary["DIRECTWRITE"] = "no";
        }

        else if (configCmdLine.at(i) == "-nis") {
            dictionary["NIS"] = "yes";
        } else if (configCmdLine.at(i) == "-no-nis") {
            dictionary["NIS"] = "no";
        }

        else if (configCmdLine.at(i) == "-cups") {
            dictionary["QT_CUPS"] = "yes";
        } else if (configCmdLine.at(i) == "-no-cups") {
            dictionary["QT_CUPS"] = "no";
        }

        else if (configCmdLine.at(i) == "-iconv") {
            dictionary["QT_ICONV"] = "yes";
        } else if (configCmdLine.at(i) == "-no-iconv") {
            dictionary["QT_ICONV"] = "no";
        } else if (configCmdLine.at(i) == "-sun-iconv") {
            dictionary["QT_ICONV"] = "sun";
        } else if (configCmdLine.at(i) == "-gnu-iconv") {
            dictionary["QT_ICONV"] = "gnu";
        }

        else if (configCmdLine.at(i) == "-neon") {
            dictionary["NEON"] = "yes";
        } else if (configCmdLine.at(i) == "-no-neon") {
            dictionary["NEON"] = "no";
        }

        else if (configCmdLine.at(i) == "-largefile") {
            dictionary["LARGE_FILE"] = "yes";
        }

        else if (configCmdLine.at(i) == "-fontconfig") {
            dictionary["FONT_CONFIG"] = "yes";
        } else if (configCmdLine.at(i) == "-no-fontconfig") {
            dictionary["FONT_CONFIG"] = "no";
        }

        else if (configCmdLine.at(i) == "-posix-ipc") {
            dictionary["POSIX_IPC"] = "yes";
        }

        else if (configCmdLine.at(i) == "-glib") {
            dictionary["QT_GLIB"] = "yes";
        }

        else if (configCmdLine.at(i) == "-sysconfdir") {
            ++i;
            if (i == argCount)
                break;

            dictionary["QT_INSTALL_SETTINGS"] = configCmdLine.at(i);
        }

        else {
            dictionary[ "HELP" ] = "yes";
            cout << "Unknown option " << configCmdLine.at(i) << endl;
            break;
        }

#endif
    }

    // Ensure that QMAKESPEC exists in the mkspecs folder
    const QString mkspecPath(sourcePath + "/mkspecs");
    QDirIterator itMkspecs(mkspecPath, QDir::AllDirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    QStringList mkspecs;

    while (itMkspecs.hasNext()) {
        QString mkspec = itMkspecs.next();
        // Remove base PATH
        mkspec.remove(0, mkspecPath.length() + 1);
        mkspecs << mkspec;
    }

    if (dictionary["QMAKESPEC"].toLower() == "features"
        || !mkspecs.contains(dictionary["QMAKESPEC"], Qt::CaseInsensitive)) {
        dictionary[ "HELP" ] = "yes";
        if (dictionary ["QMAKESPEC_FROM"] == "commandline") {
            cout << "Invalid option \"" << dictionary["QMAKESPEC"] << "\" for -platform." << endl;
        } else if (dictionary ["QMAKESPEC_FROM"] == "env") {
            cout << "QMAKESPEC environment variable is set to \"" << dictionary["QMAKESPEC"]
                 << "\" which is not a supported platform" << endl;
        } else { // was autodetected from environment
            cout << "Unable to detect the platform from environment. Use -platform command line"
                    "argument or set the QMAKESPEC environment variable and run configure again" << endl;
        }
        cout << "See the README file for a list of supported operating systems and compilers." << endl;
    } else {
        if (dictionary[ "QMAKESPEC" ].endsWith("-icc") ||
            dictionary[ "QMAKESPEC" ].endsWith("-msvc") ||
            dictionary[ "QMAKESPEC" ].endsWith("-msvc.net") ||
            dictionary[ "QMAKESPEC" ].endsWith("-msvc2002") ||
            dictionary[ "QMAKESPEC" ].endsWith("-msvc2003") ||
            dictionary[ "QMAKESPEC" ].endsWith("-msvc2005") ||
            dictionary[ "QMAKESPEC" ].endsWith("-msvc2008") ||
            dictionary[ "QMAKESPEC" ].endsWith("-msvc2010") ||
            dictionary[ "QMAKESPEC" ].endsWith("-msvc2012")) {
            if (dictionary[ "MAKE" ].isEmpty()) dictionary[ "MAKE" ] = "nmake";
            dictionary[ "QMAKEMAKEFILE" ] = "Makefile.win32";
        } else if (dictionary[ "QMAKESPEC" ] == QString("win32-g++")) {
            if (dictionary[ "MAKE" ].isEmpty()) dictionary[ "MAKE" ] = "mingw32-make";
            dictionary[ "QMAKEMAKEFILE" ] = "Makefile.win32-g++";
        } else {
            if (dictionary[ "MAKE" ].isEmpty()) dictionary[ "MAKE" ] = "make";
            dictionary[ "QMAKEMAKEFILE" ] = "Makefile.win32";
        }
    }

    // Tell the user how to proceed building Qt after configure finished its job
    dictionary["QTBUILDINSTRUCTION"] = dictionary["MAKE"];
    if (dictionary.contains("XQMAKESPEC")) {
        if (dictionary["XQMAKESPEC"].startsWith("wince")) {
            dictionary["QTBUILDINSTRUCTION"] =
                QString("setcepaths.bat ") + dictionary["XQMAKESPEC"] + QString(" && ") + dictionary["MAKE"];
        }
    }

    // Tell the user how to confclean before the next configure
    dictionary["CONFCLEANINSTRUCTION"] = dictionary["MAKE"] + QString(" confclean");

    // Ensure that -spec (XQMAKESPEC) exists in the mkspecs folder as well
    if (dictionary.contains("XQMAKESPEC") &&
        !mkspecs.contains(dictionary["XQMAKESPEC"], Qt::CaseInsensitive)) {
            dictionary["HELP"] = "yes";
            cout << "Invalid option \"" << dictionary["XQMAKESPEC"] << "\" for -xplatform." << endl;
    }

    // Ensure that the crt to be deployed can be found
    if (dictionary["CE_CRT"] != QLatin1String("yes") && dictionary["CE_CRT"] != QLatin1String("no")) {
        QDir cDir(dictionary["CE_CRT"]);
        QStringList entries = cDir.entryList();
        bool hasDebug = entries.contains("msvcr80.dll");
        bool hasRelease = entries.contains("msvcr80d.dll");
        if ((dictionary["BUILDALL"] == "auto") && (!hasDebug || !hasRelease)) {
            cout << "Could not find debug and release c-runtime." << endl;
            cout << "You need to have msvcr80.dll and msvcr80d.dll in" << endl;
            cout << "the path specified. Setting to -no-crt";
            dictionary[ "CE_CRT" ] = "no";
        } else if ((dictionary["BUILD"] == "debug") && !hasDebug) {
            cout << "Could not find debug c-runtime (msvcr80d.dll) in the directory specified." << endl;
            cout << "Setting c-runtime automatic deployment to -no-crt" << endl;
            dictionary[ "CE_CRT" ] = "no";
        } else if ((dictionary["BUILD"] == "release") && !hasRelease) {
            cout << "Could not find release c-runtime (msvcr80.dll) in the directory specified." << endl;
            cout << "Setting c-runtime automatic deployment to -no-crt" << endl;
            dictionary[ "CE_CRT" ] = "no";
        }
    }

    // Allow tests for private classes to be compiled against internal builds
    if (dictionary["BUILDDEV"] == "yes")
        qtConfig += "private_tests";

    if (dictionary["FORCE_ASSERTS"] == "yes")
        qtConfig += "force_asserts";

#if !defined(EVAL)
    for (QStringList::Iterator dis = disabledModules.begin(); dis != disabledModules.end(); ++dis) {
        modules.removeAll((*dis));
    }
    for (QStringList::Iterator ena = enabledModules.begin(); ena != enabledModules.end(); ++ena) {
        if (modules.indexOf((*ena)) == -1)
            modules += (*ena);
    }
    qtConfig += modules;

    for (QStringList::Iterator it = disabledModules.begin(); it != disabledModules.end(); ++it)
        qtConfig.removeAll(*it);

    if ((dictionary[ "REDO" ] != "yes") && (dictionary[ "HELP" ] != "yes"))
        saveCmdLine();
#endif
}

#if !defined(EVAL)
void Configure::validateArgs()
{
    // Validate the specified config

    // Get all possible configurations from the file system.
    QDir dir;
    QStringList filters;
    filters << "qconfig-*.h";
    dir.setNameFilters(filters);
    dir.setPath(sourcePath + "/src/corelib/global/");

    QStringList stringList =  dir.entryList();

    QStringList::Iterator it;
    for (it = stringList.begin(); it != stringList.end(); ++it)
        allConfigs << it->remove("qconfig-").remove(".h");
    allConfigs << "full";

    // Try internal configurations first.
    QStringList possible_configs = QStringList()
        << "minimal"
        << "small"
        << "medium"
        << "large"
        << "full";
    int index = possible_configs.indexOf(dictionary["QCONFIG"]);
    if (index >= 0) {
        for (int c = 0; c <= index; c++) {
            qmakeConfig += possible_configs[c] + "-config";
        }
        return;
    }

    // If the internal configurations failed, try others.
    QStringList::Iterator config;
    for (config = allConfigs.begin(); config != allConfigs.end(); ++config) {
        if ((*config) == dictionary[ "QCONFIG" ])
            break;
    }
    if (config == allConfigs.end()) {
        dictionary[ "HELP" ] = "yes";
        cout << "No such configuration \"" << qPrintable(dictionary[ "QCONFIG" ]) << "\"" << endl ;
    }
    else
        qmakeConfig += (*config) + "-config";
}
#endif


// Output helper functions --------------------------------[ Start ]-
/*!
    Determines the length of a string token.
*/
static int tokenLength(const char *str)
{
    if (*str == 0)
        return 0;

    const char *nextToken = strpbrk(str, " _/\n\r");
    if (nextToken == str || !nextToken)
        return 1;

    return int(nextToken - str);
}

/*!
    Prints out a string which starts at position \a startingAt, and
    indents each wrapped line with \a wrapIndent characters.
    The wrap point is set to the console width, unless that width
    cannot be determined, or is too small.
*/
void Configure::desc(const char *description, int startingAt, int wrapIndent)
{
    int linePos = startingAt;

    bool firstLine = true;
    const char *nextToken = description;
    while (*nextToken) {
        int nextTokenLen = tokenLength(nextToken);
        if (*nextToken == '\n'                         // Wrap on newline, duh
            || (linePos + nextTokenLen > outputWidth)) // Wrap at outputWidth
        {
            printf("\n");
            linePos = 0;
            firstLine = false;
            if (*nextToken == '\n')
                ++nextToken;
            continue;
        }
        if (!firstLine && linePos < wrapIndent) {  // Indent to wrapIndent
            printf("%*s", wrapIndent , "");
            linePos = wrapIndent;
            if (*nextToken == ' ') {
                ++nextToken;
                continue;
            }
        }
        printf("%.*s", nextTokenLen, nextToken);
        linePos += nextTokenLen;
        nextToken += nextTokenLen;
    }
}

/*!
    Prints out an option with its description wrapped at the
    description starting point. If \a skipIndent is true, the
    indentation to the option is not outputted (used by marked option
    version of desc()). Extra spaces between option and its
    description is filled with\a fillChar, if there's available
    space.
*/
void Configure::desc(const char *option, const char *description, bool skipIndent, char fillChar)
{
    if (!skipIndent)
        printf("%*s", optionIndent, "");

    int remaining  = descIndent - optionIndent - strlen(option);
    int wrapIndent = descIndent + qMax(0, 1 - remaining);
    printf("%s", option);

    if (remaining > 2) {
        printf(" "); // Space in front
        for (int i = remaining; i > 2; --i)
            printf("%c", fillChar); // Fill, if available space
    }
    printf(" "); // Space between option and description

    desc(description, wrapIndent, wrapIndent);
    printf("\n");
}

/*!
    Same as above, except it also marks an option with an '*', if
    the option is default action.
*/
void Configure::desc(const char *mark_option, const char *mark, const char *option, const char *description, char fillChar)
{
    const QString markedAs = dictionary.value(mark_option);
    if (markedAs == "auto" && markedAs == mark) // both "auto", always => +
        printf(" +  ");
    else if (markedAs == "auto")                // setting marked as "auto" and option is default => +
        printf(" %c  " , (defaultTo(mark_option) == QLatin1String(mark))? '+' : ' ');
    else if (QLatin1String(mark) == "auto" && markedAs != "no")     // description marked as "auto" and option is available => +
        printf(" %c  " , checkAvailability(mark_option) ? '+' : ' ');
    else                                        // None are "auto", (markedAs == mark) => *
        printf(" %c  " , markedAs == QLatin1String(mark) ? '*' : ' ');

    desc(option, description, true, fillChar);
}

/*!
    Modifies the default configuration based on given -platform option.
    Eg. switches to different default styles for Windows CE.
*/
void Configure::applySpecSpecifics()
{
    if (!dictionary[ "XQMAKESPEC" ].isEmpty()) {
        //Disable building tools, docs and translations when cross compiling.
        nobuildParts << "docs" << "translations" << "tools";
    }

    if (dictionary[ "XQMAKESPEC" ].startsWith("wince")) {
        dictionary[ "STYLE_WINDOWSXP" ]     = "no";
        dictionary[ "STYLE_WINDOWSVISTA" ]  = "no";
        dictionary[ "STYLE_PLASTIQUE" ]     = "no";
        dictionary[ "STYLE_CLEANLOOKS" ]    = "no";
        dictionary[ "STYLE_WINDOWSCE" ]     = "yes";
        dictionary[ "STYLE_WINDOWSMOBILE" ] = "yes";
        dictionary[ "STYLE_MOTIF" ]         = "no";
        dictionary[ "STYLE_CDE" ]           = "no";
        dictionary[ "OPENGL" ]              = "no";
        dictionary[ "OPENSSL" ]             = "no";
        dictionary[ "RTTI" ]                = "no";
        dictionary[ "SSE2" ]                = "no";
        dictionary[ "SSE3" ]                = "no";
        dictionary[ "SSSE3" ]               = "no";
        dictionary[ "SSE4_1" ]              = "no";
        dictionary[ "SSE4_2" ]              = "no";
        dictionary[ "AVX" ]                 = "no";
        dictionary[ "AVX2" ]                = "no";
        dictionary[ "IWMMXT" ]              = "no";
        dictionary[ "CE_CRT" ]              = "yes";
        dictionary[ "LARGE_FILE" ]          = "no";
        // We only apply MMX/IWMMXT for mkspecs we know they work
        if (dictionary[ "XQMAKESPEC" ].startsWith("wincewm")) {
            dictionary[ "MMX" ]    = "yes";
            dictionary[ "IWMMXT" ] = "yes";
        }
    } else if (dictionary[ "XQMAKESPEC" ].startsWith("linux")) { //TODO actually wrong.
      //TODO
        dictionary[ "STYLE_WINDOWSXP" ]     = "no";
        dictionary[ "STYLE_WINDOWSVISTA" ]  = "no";
        dictionary[ "KBD_DRIVERS" ]         = "tty";
        dictionary[ "GFX_DRIVERS" ]         = "linuxfb";
        dictionary[ "MOUSE_DRIVERS" ]       = "pc linuxtp";
        dictionary[ "OPENGL" ]              = "no";
        dictionary[ "DBUS"]                 = "no";
        dictionary[ "QT_INOTIFY" ]          = "no";
        dictionary[ "QT_CUPS" ]             = "no";
        dictionary[ "QT_GLIB" ]             = "no";
        dictionary[ "QT_ICONV" ]            = "no";

        dictionary["DECORATIONS"]           = "default windows styled";
    }
}

QString Configure::locateFileInPaths(const QString &fileName, const QStringList &paths)
{
    QDir d;
    for (QStringList::ConstIterator it = paths.begin(); it != paths.end(); ++it) {
        // Remove any leading or trailing ", this is commonly used in the environment
        // variables
        QString path = (*it);
        if (path.startsWith("\""))
            path = path.right(path.length() - 1);
        if (path.endsWith("\""))
            path = path.left(path.length() - 1);
        if (d.exists(path + QDir::separator() + fileName)) {
            return (path);
        }
    }
    return QString();
}

QString Configure::locateFile(const QString &fileName)
{
    QString file = fileName.toLower();
    QStringList paths;
#if defined(Q_OS_WIN32)
    QRegExp splitReg("[;,]");
#else
    QRegExp splitReg("[:]");
#endif
    if (file.endsWith(".h"))
        paths = QString::fromLocal8Bit(getenv("INCLUDE")).split(splitReg, QString::SkipEmptyParts);
    else if (file.endsWith(".lib"))
        paths = QString::fromLocal8Bit(getenv("LIB")).split(splitReg, QString::SkipEmptyParts);
    else
        paths = QString::fromLocal8Bit(getenv("PATH")).split(splitReg, QString::SkipEmptyParts);
    return locateFileInPaths(file, paths);
}

// Output helper functions ---------------------------------[ Stop ]-


bool Configure::displayHelp()
{
    if (dictionary[ "HELP" ] == "yes") {
        desc("Usage: configure [options]\n\n", 0, 7);

        desc("Installation options:\n\n");

        desc("These are optional, but you may specify install directories.\n\n", 0, 1);

        desc(       "-prefix <dir>",                    "This will install everything relative to <dir> (default $QT_INSTALL_PREFIX)\n\n");

        desc(       "-hostprefix [dir]",                "Tools and libraries needed when developing applications are installed in [dir]. "
                                                        "If [dir] is not given, the current build directory will be used. (default PREFIX)\n");

        desc("You may use these to separate different parts of the install:\n\n");

        desc(       "-bindir <dir>",                    "Executables will be installed to <dir> (default PREFIX/bin)");
        desc(       "-libdir <dir>",                    "Libraries will be installed to <dir> (default PREFIX/lib)");
        desc(       "-docdir <dir>",                    "Documentation will be installed to <dir> (default PREFIX/doc)");
        desc(       "-headerdir <dir>",                 "Headers will be installed to <dir> (default PREFIX/include)");
        desc(       "-plugindir <dir>",                 "Plugins will be installed to <dir> (default PREFIX/plugins)");
        desc(       "-importdir <dir>",                 "Imports for QML will be installed to <dir> (default PREFIX/imports)");
        desc(       "-datadir <dir>",                   "Data used by Qt programs will be installed to <dir> (default PREFIX)");
        desc(       "-translationdir <dir>",            "Translations of Qt programs will be installed to <dir> (default PREFIX/translations)");
        desc(       "-examplesdir <dir>",               "Examples will be installed to <dir> (default PREFIX/examples)");
        desc(       "-testsdir <dir>",                  "Tests will be installed to <dir> (default PREFIX/tests)");

        desc(       "-hostbindir <dir>",                "Host executables will be installed to <dir> (default HOSTPREFIX/bin)");
        desc(       "-hostdatadir <dir>",               "Data used by qmake will be installed to <dir> (default HOSTPREFIX)");

#if !defined(EVAL)
        desc("Configure options:\n\n");

        desc(" The defaults (*) are usually acceptable. A plus (+) denotes a default value"
             " that needs to be evaluated. If the evaluation succeeds, the feature is"
             " included. Here is a short explanation of each option:\n\n", 0, 1);

        desc("BUILD", "release","-release",             "Compile and link Qt with debugging turned off.");
        desc("BUILD", "debug",  "-debug",               "Compile and link Qt with debugging turned on.");
        desc("BUILDALL", "yes", "-debug-and-release",   "Compile and link two Qt libraries, with and without debugging turned on.\n");

        desc("FORCEDEBUGINFO", "yes","-force-debug-info", "Create symbol files for release builds.\n");

        desc("BUILDDEV", "yes", "-developer-build",      "Compile and link Qt with Qt developer options (including auto-tests exporting)\n");

        desc("OPENSOURCE", "opensource", "-opensource",   "Compile and link the Open-Source Edition of Qt.");
        desc("COMMERCIAL", "commercial", "-commercial",   "Compile and link the Commercial Edition of Qt.\n");

        desc("SHARED", "yes",   "-shared",              "Create and use shared Qt libraries.");
        desc("SHARED", "no",    "-static",              "Create and use static Qt libraries.\n");

        desc("LTCG", "yes",   "-ltcg",                  "Use Link Time Code Generation. (Release builds only)");
        desc("LTCG", "no",    "-no-ltcg",               "Do not use Link Time Code Generation.\n");

        desc("FAST", "no",      "-no-fast",             "Configure Qt normally by generating Makefiles for all project files.");
        desc("FAST", "yes",     "-fast",                "Configure Qt quickly by generating Makefiles only for library and "
                                                        "subdirectory targets.  All other Makefiles are created as wrappers "
                                                        "which will in turn run qmake\n");

        desc(                   "-make <part>",         "Add part to the list of parts to be built at make time.");
        for (int i=0; i<defaultBuildParts.size(); ++i)
            desc(               "",                     qPrintable(QString("  %1").arg(defaultBuildParts.at(i))), false, ' ');
        desc(                   "-nomake <part>",       "Exclude part from the list of parts to be built.\n");

        desc("WIDGETS", "no", "-no-widgets",            "Disable QtWidgets module\n");

        desc("ACCESSIBILITY", "no",  "-no-accessibility", "Do not compile Windows Active Accessibility support.");
        desc("ACCESSIBILITY", "yes", "-accessibility",    "Compile Windows Active Accessibility support.\n");

        desc(                   "-no-sql-<driver>",     "Disable SQL <driver> entirely, by default none are turned on.");
        desc(                   "-qt-sql-<driver>",     "Enable a SQL <driver> in the Qt Library.");
        desc(                   "-plugin-sql-<driver>", "Enable SQL <driver> as a plugin to be linked to at run time.\n"
                                                        "Available values for <driver>:");
        desc("SQL_MYSQL", "auto", "",                   "  mysql", ' ');
        desc("SQL_PSQL", "auto", "",                    "  psql", ' ');
        desc("SQL_OCI", "auto", "",                     "  oci", ' ');
        desc("SQL_ODBC", "auto", "",                    "  odbc", ' ');
        desc("SQL_TDS", "auto", "",                     "  tds", ' ');
        desc("SQL_DB2", "auto", "",                     "  db2", ' ');
        desc("SQL_SQLITE", "auto", "",                  "  sqlite", ' ');
        desc("SQL_SQLITE2", "auto", "",                 "  sqlite2", ' ');
        desc("SQL_IBASE", "auto", "",                   "  ibase", ' ');
        desc(                   "",                     "(drivers marked with a '+' have been detected as available on this system)\n", false, ' ');

        desc(                   "-system-sqlite",       "Use sqlite from the operating system.\n");

        desc("OPENGL", "no","-no-opengl",               "Disables OpenGL functionality\n");
        desc("OPENGL", "no","-opengl <api>",            "Enable OpenGL support with specified API version.\n"
                                                        "Available values for <api>:");
        desc("", "", "",                                "  desktop - Enable support for Desktop OpenGL", ' ');
        desc("OPENGL_ES_CM", "no", "",                  "  es1 - Enable support for OpenGL ES Common Profile", ' ');
        desc("OPENGL_ES_2",  "no", "",                  "  es2 - Enable support for OpenGL ES 2.0", ' ');

        desc("OPENVG", "no","-no-openvg",               "Disables OpenVG functionality\n");
        desc("OPENVG", "yes","-openvg",                 "Enables OpenVG functionality");
        desc(                   "-force-asserts",       "Activate asserts in release mode.\n");
#endif
        desc(                   "-platform <spec>",     "The operating system and compiler you are building on.\n(default %QMAKESPEC%)\n");
        desc(                   "-xplatform <spec>",    "The operating system and compiler you are cross compiling to.\n");
        desc(                   "",                     "See the README file for a list of supported operating systems and compilers.\n", false, ' ');
        desc(                   "-sysroot <dir>",       "Sets <dir> as the target compiler's and qmake's sysroot and also sets pkg-config paths.");
        desc(                   "-no-gcc-sysroot",      "When using -sysroot, it disables the passing of --sysroot to the compiler ");

        desc("NIS",  "no",      "-no-nis",              "Do not build NIS support.");
        desc("NIS",  "yes",     "-nis",                 "Build NIS support.");

        desc("NEON", "yes",     "-neon",                "Enable the use of NEON instructions.");
        desc("NEON", "no",      "-no-neon",             "Do not enable the use of NEON instructions.");

        desc("QT_ICONV",    "disable", "-no-iconv",     "Do not enable support for iconv(3).");
        desc("QT_ICONV",    "yes",     "-iconv",        "Enable support for iconv(3).");
        desc("QT_ICONV",    "yes",     "-sun-iconv",    "Enable support for iconv(3) using sun-iconv.");
        desc("QT_ICONV",    "yes",     "-gnu-iconv",    "Enable support for iconv(3) using gnu-libiconv");

        desc("LARGE_FILE",  "yes",     "-largefile",    "Enables Qt to access files larger than 4 GB.");

        desc("FONT_CONFIG", "yes",     "-fontconfig",   "Build with FontConfig support.");
        desc("FONT_CONFIG", "no",      "-no-fontconfig" "Do not build with FontConfig support.");

        desc("POSIX_IPC",   "yes",     "-posix-ipc",    "Enable POSIX IPC.");

        desc("QT_GLIB",     "yes",     "-glib",         "Enable Glib support.");

        desc("QT_INSTALL_SETTINGS", "auto", "-sysconfdir", "Settings used by Qt programs will be looked for in <dir>.");

#if !defined(EVAL)
        desc(                   "-qtnamespace <namespace>", "Wraps all Qt library code in 'namespace name {...}");
        desc(                   "-qtlibinfix <infix>",  "Renames all Qt* libs to Qt*<infix>\n");
        desc(                   "-D <define>",          "Add an explicit define to the preprocessor.");
        desc(                   "-I <includepath>",     "Add an explicit include path.");
        desc(                   "-L <librarypath>",     "Add an explicit library path.");
        desc(                   "-l <libraryname>",     "Add an explicit library name, residing in a librarypath.\n");
#endif

        desc(                   "-help, -h, -?",        "Display this information.\n");

#if !defined(EVAL)
        // 3rd party stuff options go below here --------------------------------------------------------------------------------
        desc("Third Party Libraries:\n\n");

        desc("ZLIB", "qt",      "-qt-zlib",             "Use the zlib bundled with Qt.");
        desc("ZLIB", "system",  "-system-zlib",         "Use zlib from the operating system.\nSee http://www.gzip.org/zlib\n");

        desc("PCRE", "qt",       "-qt-pcre",            "Use the PCRE library bundled with Qt.");
        desc("PCRE", "qt",       "-system-pcre",        "Use the PCRE library from the operating system.\nSee http://pcre.org/\n");

        desc("ICU", "yes",       "-icu",                "Use the ICU library.");
        desc("ICU", "no",        "-no-icu",             "Do not use the ICU library.\nSee http://site.icu-project.org/\n");

        desc("GIF", "no",       "-no-gif",              "Do not compile GIF reading support.");

        desc("LIBPNG", "no",    "-no-libpng",           "Do not compile PNG support.");
        desc("LIBPNG", "qt",    "-qt-libpng",           "Use the libpng bundled with Qt.");
        desc("LIBPNG", "system","-system-libpng",       "Use libpng from the operating system.\nSee http://www.libpng.org/pub/png\n");

        desc("LIBJPEG", "no",    "-no-libjpeg",         "Do not compile JPEG support.");
        desc("LIBJPEG", "qt",    "-qt-libjpeg",         "Use the libjpeg bundled with Qt.");
        desc("LIBJPEG", "system","-system-libjpeg",     "Use libjpeg from the operating system.\nSee http://www.ijg.org\n");

        desc("FREETYPE", "no",   "-no-freetype",        "Do not compile in Freetype2 support.");
        desc("FREETYPE", "yes",  "-qt-freetype",        "Use the libfreetype bundled with Qt.");
        desc("FREETYPE", "yes",  "-system-freetype",    "Use the libfreetype provided by the system.");
#endif
        // Qt\Windows only options go below here --------------------------------------------------------------------------------
        desc("Qt for Windows only:\n\n");

        desc("VCPROJFILES", "no", "-no-vcproj",         "Do not generate VC++ .vcproj files.");
        desc("VCPROJFILES", "yes", "-vcproj",           "Generate VC++ .vcproj files, only if platform \"win32-msvc.net\".\n");

        desc("INCREDIBUILD_XGE", "no", "-no-incredibuild-xge", "Do not add IncrediBuild XGE distribution commands to custom build steps.");
        desc("INCREDIBUILD_XGE", "yes", "-incredibuild-xge",   "Add IncrediBuild XGE distribution commands to custom build steps. This will distribute MOC and UIC steps, and other custom buildsteps which are added to the INCREDIBUILD_XGE variable.\n(The IncrediBuild distribution commands are only added to Visual Studio projects)\n");

        desc("PLUGIN_MANIFESTS", "no", "-no-plugin-manifests", "Do not embed manifests in plugins.");
        desc("PLUGIN_MANIFESTS", "yes", "-plugin-manifests",   "Embed manifests in plugins.\n");
        desc(       "-angle <dir>",                     "Use ANGLE library from location <dir>.\n");
#if !defined(EVAL)
        desc("BUILD_QMAKE", "no", "-no-qmake",          "Do not compile qmake.");
        desc("BUILD_QMAKE", "yes", "-qmake",            "Compile qmake.\n");

        desc("PROCESS", "partial", "-process",          "Generate top-level Makefiles/Project files.\n");
        desc("PROCESS", "full", "-fully-process",       "Generate Makefiles/Project files for the entire Qt tree.\n");
        desc("PROCESS", "no", "-dont-process",          "Do not generate Makefiles/Project files. This will override -no-fast if specified.");

        desc("RTTI", "no",      "-no-rtti",             "Do not compile runtime type information.");
        desc("RTTI", "yes",     "-rtti",                "Compile runtime type information.\n");
        desc("SSE2", "no",      "-no-sse2",             "Do not compile with use of SSE2 instructions");
        desc("SSE2", "yes",     "-sse2",                "Compile with use of SSE2 instructions");
        desc("SSE3", "no",      "-no-sse3",             "Do not compile with use of SSE3 instructions");
        desc("SSE3", "yes",     "-sse3",                "Compile with use of SSE3 instructions");
        desc("SSSE3", "no",     "-no-ssse3",            "Do not compile with use of SSSE3 instructions");
        desc("SSSE3", "yes",    "-ssse3",               "Compile with use of SSSE3 instructions");
        desc("SSE4_1", "no",    "-no-sse4.1",           "Do not compile with use of SSE4.1 instructions");
        desc("SSE4_1", "yes",   "-sse4.1",              "Compile with use of SSE4.1 instructions");
        desc("SSE4_2", "no",    "-no-sse4.2",           "Do not compile with use of SSE4.2 instructions");
        desc("SSE4_2", "yes",   "-sse4.2",              "Compile with use of SSE4.2 instructions");
        desc("AVX", "no",       "-no-avx",              "Do not compile with use of AVX instructions");
        desc("AVX", "yes",      "-avx",                 "Compile with use of AVX instructions");
        desc("AVX2", "no",      "-no-avx2",             "Do not compile with use of AVX2 instructions");
        desc("AVX2", "yes",     "-avx2",                "Compile with use of AVX2 instructions");
        desc("OPENSSL", "no",    "-no-openssl",         "Do not compile in OpenSSL support");
        desc("OPENSSL", "yes",   "-openssl",            "Compile in run-time OpenSSL support");
        desc("OPENSSL", "linked","-openssl-linked",     "Compile in linked OpenSSL support");
        desc("DBUS", "no",       "-no-dbus",            "Do not compile in D-Bus support");
        desc("DBUS", "yes",      "-dbus",               "Compile in D-Bus support and load libdbus-1 dynamically");
        desc("DBUS", "linked",   "-dbus-linked",        "Compile in D-Bus support and link to libdbus-1");
        desc("AUDIO_BACKEND", "no","-no-audio-backend", "Do not compile in the platform audio backend into QtMultimedia");
        desc("AUDIO_BACKEND", "yes","-audio-backend",   "Compile in the platform audio backend into QtMultimedia");
        desc("QML_DEBUG", "no",    "-no-qml-debug",     "Do not build the QML debugging support");
        desc("QML_DEBUG", "yes",   "-qml-debug",        "Build the QML debugging support");
        desc("DIRECTWRITE", "no", "-no-directwrite", "Do not build support for DirectWrite font rendering");
        desc("DIRECTWRITE", "yes", "-directwrite", "Build support for DirectWrite font rendering (experimental, requires DirectWrite availability on target systems, e.g. Windows Vista with Platform Update, Windows 7, etc.)");

        desc(                   "-no-style-<style>",    "Disable <style> entirely.");
        desc(                   "-qt-style-<style>",    "Enable <style> in the Qt Library.\nAvailable styles: ");

        desc("STYLE_WINDOWS", "yes", "",                "  windows", ' ');
        desc("STYLE_WINDOWSXP", "auto", "",             "  windowsxp", ' ');
        desc("STYLE_WINDOWSVISTA", "auto", "",          "  windowsvista", ' ');
        desc("STYLE_PLASTIQUE", "yes", "",              "  plastique", ' ');
        desc("STYLE_CLEANLOOKS", "yes", "",             "  cleanlooks", ' ');
        desc("STYLE_MOTIF", "yes", "",                  "  motif", ' ');
        desc("STYLE_CDE", "yes", "",                    "  cde", ' ');
        desc("STYLE_WINDOWSCE", "yes", "",              "  windowsce", ' ');
        desc("STYLE_WINDOWSMOBILE" , "yes", "",         "  windowsmobile", ' ');
        desc("NATIVE_GESTURES", "no", "-no-native-gestures", "Do not use native gestures on Windows 7.");
        desc("NATIVE_GESTURES", "yes", "-native-gestures", "Use native gestures on Windows 7.");
        desc("MSVC_MP", "no", "-no-mp",                 "Do not use multiple processors for compiling with MSVC");
        desc("MSVC_MP", "yes", "-mp",                   "Use multiple processors for compiling with MSVC (-MP)");

/*      We do not support -qconfig on Windows yet

        desc(                   "-qconfig <local>",     "Use src/tools/qconfig-local.h rather than the default.\nPossible values for local:");
        for (int i=0; i<allConfigs.size(); ++i)
            desc(               "",                     qPrintable(QString("  %1").arg(allConfigs.at(i))), false, ' ');
        printf("\n");
*/
#endif
        desc(                   "-loadconfig <config>", "Run configure with the parameters from file configure_<config>.cache.");
        desc(                   "-saveconfig <config>", "Run configure and save the parameters in file configure_<config>.cache.");
        desc(                   "-redo",                "Run configure with the same parameters as last time.\n");

        // Qt\Windows CE only options go below here -----------------------------------------------------------------------------
        desc("Qt for Windows CE only:\n\n");
        desc("IWMMXT", "no",       "-no-iwmmxt",           "Do not compile with use of IWMMXT instructions");
        desc("IWMMXT", "yes",      "-iwmmxt",              "Do compile with use of IWMMXT instructions (Qt for Windows CE on Arm only)");
        desc("CE_CRT", "no",       "-no-crt" ,             "Do not add the C runtime to default deployment rules");
        desc("CE_CRT", "yes",      "-qt-crt",              "Qt identifies C runtime during project generation");
        desc(                      "-crt <path>",          "Specify path to C runtime used for project generation.");
        desc("CETEST", "no",       "-no-cetest",           "Do not compile Windows CE remote test application");
        desc("CETEST", "yes",      "-cetest",              "Compile Windows CE remote test application");
        desc(                      "-signature <file>",    "Use file for signing the target project");
        return true;
    }
    return false;
}

QString Configure::findFileInPaths(const QString &fileName, const QString &paths)
{
#if defined(Q_OS_WIN32)
    QRegExp splitReg("[;,]");
#else
    QRegExp splitReg("[:]");
#endif
    QStringList pathList = paths.split(splitReg, QString::SkipEmptyParts);
    QDir d;
    for (QStringList::ConstIterator it = pathList.begin(); it != pathList.end(); ++it) {
        // Remove any leading or trailing ", this is commonly used in the environment
        // variables
        QString path = (*it);
        if (path.startsWith('\"'))
            path = path.right(path.length() - 1);
        if (path.endsWith('\"'))
            path = path.left(path.length() - 1);
        if (d.exists(path + QDir::separator() + fileName))
            return path;
    }
    return QString();
}

static QString mingwPaths(const QString &mingwPath, const QString &pathName)
{
    QString ret;
    QDir mingwDir = QFileInfo(mingwPath).dir();
    const QFileInfoList subdirs = mingwDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (int i = 0 ;i < subdirs.length(); ++i) {
        const QFileInfo &fi = subdirs.at(i);
        const QString name = fi.fileName();
        if (name == pathName)
            ret += fi.absoluteFilePath() + ';';
        else if (name.contains("mingw"))
            ret += fi.absoluteFilePath() + QDir::separator() + pathName + ';';
    }
    return ret;
}

bool Configure::findFile(const QString &fileName)
{
    const QString file = fileName.toLower();
    const QString pathEnvVar = QString::fromLocal8Bit(getenv("PATH"));
    const QString mingwPath = dictionary["QMAKESPEC"].endsWith("-g++") ?
        findFileInPaths("g++.exe", pathEnvVar) : QString();

    QString paths;
    if (file.endsWith(".h")) {
        if (!mingwPath.isNull()) {
            if (!findFileInPaths(file, mingwPaths(mingwPath, "include")).isNull())
                return true;
            //now let's try the additional compiler path

            const QFileInfoList mingwConfigs = QDir(mingwPath + QLatin1String("/../lib/gcc")).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (int i = 0; i < mingwConfigs.length(); ++i) {
                const QDir mingwLibDir = mingwConfigs.at(i).absoluteFilePath();
                foreach(const QFileInfo &version, mingwLibDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    if (!findFileInPaths(file, version.absoluteFilePath() + QLatin1String("/include")).isNull())
                        return true;
                }
            }
        }
        paths = QString::fromLocal8Bit(getenv("INCLUDE"));
    } else if (file.endsWith(".lib") ||  file.endsWith(".a")) {
        if (!mingwPath.isNull() && !findFileInPaths(file, mingwPaths(mingwPath, "lib")).isNull())
            return true;
        paths = QString::fromLocal8Bit(getenv("LIB"));
    } else {
        paths = pathEnvVar;
    }
    return !findFileInPaths(file, paths).isNull();
}

/*!
    Default value for options marked as "auto" if the test passes.
    (Used both by the autoDetection() below, and the desc() function
    to mark (+) the default option of autodetecting options.
*/
QString Configure::defaultTo(const QString &option)
{
    // We prefer using the system version of the 3rd party libs
    if (option == "ZLIB"
        || option == "PCRE"
        || option == "LIBJPEG"
        || option == "LIBPNG")
        return "system";

    // PNG is always built-in, never a plugin
    if (option == "PNG")
        return "yes";

    // These database drivers and image formats can be built-in or plugins.
    // Prefer plugins when Qt is shared.
    if (dictionary[ "SHARED" ] == "yes") {
        if (option == "SQL_MYSQL"
            || option == "SQL_MYSQL"
            || option == "SQL_ODBC"
            || option == "SQL_OCI"
            || option == "SQL_PSQL"
            || option == "SQL_TDS"
            || option == "SQL_DB2"
            || option == "SQL_SQLITE"
            || option == "SQL_SQLITE2"
            || option == "SQL_IBASE"
            || option == "JPEG"
            || option == "GIF")
            return "plugin";
    }

    // By default we do not want to compile OCI driver when compiling with
    // MinGW, due to lack of such support from Oracle. It prob. wont work.
    // (Customer may force the use though)
    if (dictionary["QMAKESPEC"].endsWith("-g++")
        && option == "SQL_OCI")
        return "no";

    if (option == "SYNCQT"
        && (!QFile::exists(sourcePath + "/bin/syncqt") ||
            !QFile::exists(sourcePath + "/bin/syncqt.bat")))
        return "no";

    return "yes";
}

/*!
    Checks the system for the availability of a feature.
    Returns true if the feature is available, else false.
*/
bool Configure::checkAvailability(const QString &part)
{
    bool available = false;
    if (part == "STYLE_WINDOWSXP")
        available = (platform() == WINDOWS) && findFile("uxtheme.h");

    else if (part == "ZLIB")
        available = findFile("zlib.h");

    else if (part == "PCRE")
        available = findFile("pcre.h");

    else if (part == "ICU")
        available = findFile("unicode/utypes.h") && findFile("unicode/ucol.h") && findFile("unicode/ustring.h") && findFile("icuin.lib");

    else if (part == "LIBJPEG")
        available = findFile("jpeglib.h");
    else if (part == "LIBPNG")
        available = findFile("png.h");
    else if (part == "SQL_MYSQL")
        available = findFile("mysql.h") && findFile("libmySQL.lib");
    else if (part == "SQL_ODBC")
        available = findFile("sql.h") && findFile("sqlext.h") && findFile("odbc32.lib");
    else if (part == "SQL_OCI")
        available = findFile("oci.h") && findFile("oci.lib");
    else if (part == "SQL_PSQL")
        available = findFile("libpq-fe.h") && findFile("libpq.lib") && findFile("ws2_32.lib") && findFile("advapi32.lib");
    else if (part == "SQL_TDS")
        available = findFile("sybfront.h") && findFile("sybdb.h") && findFile("ntwdblib.lib");
    else if (part == "SQL_DB2")
        available = findFile("sqlcli.h") && findFile("sqlcli1.h") && findFile("db2cli.lib");
    else if (part == "SQL_SQLITE")
        available = true; // Built in, we have a fork
    else if (part == "SQL_SQLITE_LIB") {
        if (dictionary[ "SQL_SQLITE_LIB" ] == "system") {
            if ((platform() == QNX) || (platform() == BLACKBERRY)) {
                available = true;
                dictionary[ "QT_LFLAGS_SQLITE" ] += "-lsqlite3 -lz";
            } else {
                available = findFile("sqlite3.h") && findFile("sqlite3.lib");
                if (available)
                    dictionary[ "QT_LFLAGS_SQLITE" ] += "sqlite3.lib";
            }
        } else {
            available = true;
        }
    } else if (part == "SQL_SQLITE2")
        available = findFile("sqlite.h") && findFile("sqlite.lib");
    else if (part == "SQL_IBASE")
        available = findFile("ibase.h") && (findFile("gds32_ms.lib") || findFile("gds32.lib"));
    else if (part == "IWMMXT")
        available = (dictionary.value("XQMAKESPEC").startsWith("wince"));
    else if (part == "OPENGL_ES_CM")
        available = (dictionary.value("XQMAKESPEC").startsWith("wince"));
    else if (part == "OPENGL_ES_2")
        available = (dictionary.value("XQMAKESPEC").startsWith("wince"));
    else if (part == "SSE2")
        available = tryCompileProject("common/sse2");
    else if (part == "SSE3")
        available = tryCompileProject("common/sse3");
    else if (part == "SSSE3")
        available = tryCompileProject("common/ssse3");
    else if (part == "SSE4_1")
        available = tryCompileProject("common/sse4_1");
    else if (part == "SSE4_2")
        available = tryCompileProject("common/sse4_2");
    else if (part == "AVX")
        available = tryCompileProject("common/avx");
    else if (part == "AVX2")
        available = tryCompileProject("common/avx2");
    else if (part == "OPENSSL")
        available = findFile("openssl\\ssl.h");
    else if (part == "DBUS")
        available = findFile("dbus\\dbus.h");
    else if (part == "CETEST") {
        QString rapiHeader = locateFile("rapi.h");
        QString rapiLib = locateFile("rapi.lib");
        available = (dictionary.value("XQMAKESPEC").startsWith("wince")) && !rapiHeader.isEmpty() && !rapiLib.isEmpty();
        if (available) {
            dictionary[ "QT_CE_RAPI_INC" ] += QLatin1String("\"") + rapiHeader + QLatin1String("\"");
            dictionary[ "QT_CE_RAPI_LIB" ] += QLatin1String("\"") + rapiLib + QLatin1String("\"");
        }
        else if (dictionary[ "CETEST_REQUESTED" ] == "yes") {
            cout << "cetest could not be enabled: rapi.h and rapi.lib could not be found." << endl;
            cout << "Make sure the environment is set up for compiling with ActiveSync." << endl;
            dictionary[ "DONE" ] = "error";
        }
    } else if (part == "INCREDIBUILD_XGE") {
        available = findFile("BuildConsole.exe") && findFile("xgConsole.exe");
    } else if (part == "WMSDK") {
        available = findFile("wmsdk.h");
    } else if (part == "V8SNAPSHOT") {
        available = true;
    } else if (part == "AUDIO_BACKEND") {
        available = true;
    } else if (part == "DIRECTWRITE") {
        available = findFile("dwrite.h") && findFile("d2d1.h") && findFile("dwrite.lib");
    } else if (part == "ICONV") {
        available = tryCompileProject("unix/iconv") || tryCompileProject("unix/gnu-libiconv");
    } else if (part == "CUPS") {
        available = (platform() != WINDOWS) && (platform() != WINDOWS_CE) && tryCompileProject("unix/cups");
    }

    return available;
}

/*
    Autodetect options marked as "auto".
*/
void Configure::autoDetection()
{
    // Style detection
    if (dictionary["STYLE_WINDOWSXP"] == "auto")
        dictionary["STYLE_WINDOWSXP"] = checkAvailability("STYLE_WINDOWSXP") ? defaultTo("STYLE_WINDOWSXP") : "no";
    if (dictionary["STYLE_WINDOWSVISTA"] == "auto") // Vista style has the same requirements as XP style
        dictionary["STYLE_WINDOWSVISTA"] = checkAvailability("STYLE_WINDOWSXP") ? defaultTo("STYLE_WINDOWSVISTA") : "no";

    // Compression detection
    if (dictionary["ZLIB"] == "auto")
        dictionary["ZLIB"] =  checkAvailability("ZLIB") ? defaultTo("ZLIB") : "qt";

    // PCRE detection
    if (dictionary["PCRE"] == "auto")
        dictionary["PCRE"] = checkAvailability("PCRE") ? defaultTo("PCRE") : "qt";

    // ICU detection
    if (dictionary["ICU"] == "auto")
        dictionary["ICU"] = checkAvailability("ICU") ? "yes" : "no";

    // Image format detection
    if (dictionary["GIF"] == "auto")
        dictionary["GIF"] = defaultTo("GIF");
    if (dictionary["JPEG"] == "auto")
        dictionary["JPEG"] = defaultTo("JPEG");
    if (dictionary["PNG"] == "auto")
        dictionary["PNG"] = defaultTo("PNG");
    if (dictionary["LIBJPEG"] == "auto")
        dictionary["LIBJPEG"] = checkAvailability("LIBJPEG") ? defaultTo("LIBJPEG") : "qt";
    if (dictionary["LIBPNG"] == "auto")
        dictionary["LIBPNG"] = checkAvailability("LIBPNG") ? defaultTo("LIBPNG") : "qt";

    // SQL detection (not on by default)
    if (dictionary["SQL_MYSQL"] == "auto")
        dictionary["SQL_MYSQL"] = checkAvailability("SQL_MYSQL") ? defaultTo("SQL_MYSQL") : "no";
    if (dictionary["SQL_ODBC"] == "auto")
        dictionary["SQL_ODBC"] = checkAvailability("SQL_ODBC") ? defaultTo("SQL_ODBC") : "no";
    if (dictionary["SQL_OCI"] == "auto")
        dictionary["SQL_OCI"] = checkAvailability("SQL_OCI") ? defaultTo("SQL_OCI") : "no";
    if (dictionary["SQL_PSQL"] == "auto")
        dictionary["SQL_PSQL"] = checkAvailability("SQL_PSQL") ? defaultTo("SQL_PSQL") : "no";
    if (dictionary["SQL_TDS"] == "auto")
        dictionary["SQL_TDS"] = checkAvailability("SQL_TDS") ? defaultTo("SQL_TDS") : "no";
    if (dictionary["SQL_DB2"] == "auto")
        dictionary["SQL_DB2"] = checkAvailability("SQL_DB2") ? defaultTo("SQL_DB2") : "no";
    if (dictionary["SQL_SQLITE"] == "auto")
        dictionary["SQL_SQLITE"] = checkAvailability("SQL_SQLITE") ? defaultTo("SQL_SQLITE") : "no";
    if (dictionary["SQL_SQLITE_LIB"] == "system")
        if (!checkAvailability("SQL_SQLITE_LIB"))
            dictionary["SQL_SQLITE_LIB"] = "no";
    if (dictionary["SQL_SQLITE2"] == "auto")
        dictionary["SQL_SQLITE2"] = checkAvailability("SQL_SQLITE2") ? defaultTo("SQL_SQLITE2") : "no";
    if (dictionary["SQL_IBASE"] == "auto")
        dictionary["SQL_IBASE"] = checkAvailability("SQL_IBASE") ? defaultTo("SQL_IBASE") : "no";
    if (dictionary["SSE2"] == "auto")
        dictionary["SSE2"] = checkAvailability("SSE2") ? "yes" : "no";
    if (dictionary["SSE3"] == "auto")
        dictionary["SSE3"] = checkAvailability("SSE3") ? "yes" : "no";
    if (dictionary["SSSE3"] == "auto")
        dictionary["SSSE3"] = checkAvailability("SSSE3") ? "yes" : "no";
    if (dictionary["SSE4_1"] == "auto")
        dictionary["SSE4_1"] = checkAvailability("SSE4_1") ? "yes" : "no";
    if (dictionary["SSE4_2"] == "auto")
        dictionary["SSE4_2"] = checkAvailability("SSE4_2") ? "yes" : "no";
    if (dictionary["AVX"] == "auto")
        dictionary["AVX"] = checkAvailability("AVX") ? "yes" : "no";
    if (dictionary["AVX2"] == "auto")
        dictionary["AVX2"] = checkAvailability("AVX2") ? "yes" : "no";
    if (dictionary["IWMMXT"] == "auto")
        dictionary["IWMMXT"] = checkAvailability("IWMMXT") ? "yes" : "no";
    if (dictionary["OPENSSL"] == "auto")
        dictionary["OPENSSL"] = checkAvailability("OPENSSL") ? "yes" : "no";
    if (dictionary["DBUS"] == "auto")
        dictionary["DBUS"] = checkAvailability("DBUS") ? "yes" : "no";
    if (dictionary["V8SNAPSHOT"] == "auto")
        dictionary["V8SNAPSHOT"] = (dictionary["V8"] == "yes") && checkAvailability("V8SNAPSHOT") ? "yes" : "no";
    if (dictionary["QML_DEBUG"] == "auto")
        dictionary["QML_DEBUG"] = dictionary["QML"] == "yes" ? "yes" : "no";
    if (dictionary["AUDIO_BACKEND"] == "auto")
        dictionary["AUDIO_BACKEND"] = checkAvailability("AUDIO_BACKEND") ? "yes" : "no";
    if (dictionary["WMSDK"] == "auto")
        dictionary["WMSDK"] = checkAvailability("WMSDK") ? "yes" : "no";

    // Qt/WinCE remote test application
    if (dictionary["CETEST"] == "auto")
        dictionary["CETEST"] = checkAvailability("CETEST") ? "yes" : "no";

    // Detection of IncrediBuild buildconsole
    if (dictionary["INCREDIBUILD_XGE"] == "auto")
        dictionary["INCREDIBUILD_XGE"] = checkAvailability("INCREDIBUILD_XGE") ? "yes" : "no";

    // Detection of iconv support
    if (dictionary["QT_ICONV"] == "auto")
        dictionary["QT_ICONV"] = checkAvailability("ICONV") ? "yes" : "no";

    // Detection of cups support
    if (dictionary["QT_CUPS"] == "auto")
        dictionary["QT_CUPS"] = checkAvailability("CUPS") ? "yes" : "no";

    // Mark all unknown "auto" to the default value..
    for (QMap<QString,QString>::iterator i = dictionary.begin(); i != dictionary.end(); ++i) {
        if (i.value() == "auto")
            i.value() = defaultTo(i.key());
    }
}

bool Configure::verifyConfiguration()
{
    if (dictionary["SQL_SQLITE_LIB"] == "no" && dictionary["SQL_SQLITE"] != "no") {
        cout << "WARNING: Configure could not detect the presence of a system SQLite3 lib." << endl
             << "Configure will therefore continue with the SQLite3 lib bundled with Qt." << endl
             << "(Press any key to continue..)";
        if (_getch() == 3) // _Any_ keypress w/no echo(eat <Enter> for stdout)
            exit(0);      // Exit cleanly for Ctrl+C

        dictionary["SQL_SQLITE_LIB"] = "qt"; // Set to Qt's bundled lib an continue
    }
    if (dictionary["QMAKESPEC"].endsWith("-g++")
        && dictionary["SQL_OCI"] != "no") {
        cout << "WARNING: Qt does not support compiling the Oracle database driver with" << endl
             << "MinGW, due to lack of such support from Oracle. Consider disabling the" << endl
             << "Oracle driver, as the current build will most likely fail." << endl;
        cout << "(Press any key to continue..)";
        if (_getch() == 3) // _Any_ keypress w/no echo(eat <Enter> for stdout)
            exit(0);      // Exit cleanly for Ctrl+C
    }
    if (dictionary["QMAKESPEC"].endsWith("win32-msvc.net")) {
        cout << "WARNING: The makespec win32-msvc.net is deprecated. Consider using" << endl
             << "win32-msvc2002 or win32-msvc2003 instead." << endl;
        cout << "(Press any key to continue..)";
        if (_getch() == 3) // _Any_ keypress w/no echo(eat <Enter> for stdout)
            exit(0);      // Exit cleanly for Ctrl+C
    }
    if (0 != dictionary["ARM_FPU_TYPE"].size()) {
            QStringList l= QStringList()
                    << "softvfp"
                    << "softvfp+vfpv2"
                    << "vfpv2";
            if (!(l.contains(dictionary["ARM_FPU_TYPE"])))
                    cout << QString("WARNING: Using unsupported fpu flag: %1").arg(dictionary["ARM_FPU_TYPE"]) << endl;
    }
    if (dictionary["DIRECTWRITE"] == "yes" && !checkAvailability("DIRECTWRITE")) {
        cout << "WARNING: To be able to compile the DirectWrite font engine you will" << endl
             << "need the Microsoft DirectWrite and Microsoft Direct2D development" << endl
             << "files such as headers and libraries." << endl
             << "(Press any key to continue..)";
        if (_getch() == 3) // _Any_ keypress w/no echo(eat <Enter> for stdout)
            exit(0);      // Exit cleanly for Ctrl+C
    }

    return true;
}

/*
 Things that affect the Qt API/ABI:
   Options:
     minimal-config small-config medium-config large-config full-config

   Options:
     debug release

 Things that do not affect the Qt API/ABI:
     system-jpeg no-jpeg jpeg
     system-png no-png png
     system-zlib no-zlib zlib
     no-gif gif
     dll staticlib

     nocrosscompiler
     GNUmake
     largefile
     nis
     nas
     tablet

     X11     : x11sm xinerama xcursor xfixes xrandr xrender fontconfig xkb
     Embedded: embedded freetype
*/
void Configure::generateBuildKey()
{
    QString spec = dictionary["QMAKESPEC"];

    QString compiler = "msvc"; // ICC is compatible
    if (spec.endsWith("-g++"))
        compiler = "mingw";
    else if (spec.endsWith("-borland"))
        compiler = "borland";

    // Build options which changes the Qt API/ABI
    QStringList build_options;
    if (!dictionary["QCONFIG"].isEmpty())
        build_options += dictionary["QCONFIG"] + "-config ";
    build_options.sort();

    // Sorted defines that start with QT_NO_
    QStringList build_defines = qmakeDefines.filter(QRegExp("^QT_NO_"));
    build_defines.sort();
}

void Configure::generateOutputVars()
{
    // Generate variables for output
    QString build = dictionary[ "BUILD" ];
    bool buildAll = (dictionary[ "BUILDALL" ] == "yes");
    if (build == "debug") {
        if (buildAll)
            qtConfig += "debug_and_release build_all release";
        qtConfig += "debug";
    } else if (build == "release") {
        if (buildAll)
            qtConfig += "debug_and_release build_all debug";
        qtConfig += "release";
    }

    if (dictionary[ "SHARED" ] == "no")
        qtConfig += "static";
    else
        qtConfig += "shared";

    if (dictionary[ "WIDGETS" ] == "no")
        qtConfig += "no-widgets";

    // Compression --------------------------------------------------
    if (dictionary[ "ZLIB" ] == "qt")
        qtConfig += "zlib";
    else if (dictionary[ "ZLIB" ] == "system")
        qtConfig += "system-zlib";

    // PCRE ---------------------------------------------------------
    if (dictionary[ "PCRE" ] == "qt")
        qmakeConfig += "pcre";

    // ICU ---------------------------------------------------------
    if (dictionary[ "ICU" ] == "yes")
        qtConfig  += "icu";

    // Image formates -----------------------------------------------
    if (dictionary[ "GIF" ] == "no")
        qtConfig += "no-gif";
    else if (dictionary[ "GIF" ] == "yes")
        qtConfig += "gif";

    if (dictionary[ "JPEG" ] == "no")
        qtConfig += "no-jpeg";
    else if (dictionary[ "JPEG" ] == "yes")
        qtConfig += "jpeg";
    if (dictionary[ "LIBJPEG" ] == "system")
        qtConfig += "system-jpeg";

    if (dictionary[ "PNG" ] == "no")
        qtConfig += "no-png";
    else if (dictionary[ "PNG" ] == "yes")
        qtConfig += "png";
    if (dictionary[ "LIBPNG" ] == "system")
        qtConfig += "system-png";

    // Text rendering --------------------------------------------------
    if (dictionary[ "FREETYPE" ] == "yes")
        qtConfig += "freetype";
    else if (dictionary[ "FREETYPE" ] == "system")
        qtConfig += "system-freetype";

    // Styles -------------------------------------------------------
    if (dictionary[ "STYLE_WINDOWS" ] == "yes")
        qmakeStyles += "windows";

    if (dictionary[ "STYLE_PLASTIQUE" ] == "yes")
        qmakeStyles += "plastique";

    if (dictionary[ "STYLE_CLEANLOOKS" ] == "yes")
        qmakeStyles += "cleanlooks";

    if (dictionary[ "STYLE_WINDOWSXP" ] == "yes")
        qmakeStyles += "windowsxp";

    if (dictionary[ "STYLE_WINDOWSVISTA" ] == "yes")
        qmakeStyles += "windowsvista";

    if (dictionary[ "STYLE_MOTIF" ] == "yes")
        qmakeStyles += "motif";

    if (dictionary[ "STYLE_SGI" ] == "yes")
        qmakeStyles += "sgi";

    if (dictionary[ "STYLE_WINDOWSCE" ] == "yes")
    qmakeStyles += "windowsce";

    if (dictionary[ "STYLE_WINDOWSMOBILE" ] == "yes")
    qmakeStyles += "windowsmobile";

    if (dictionary[ "STYLE_CDE" ] == "yes")
        qmakeStyles += "cde";

    // Databases ----------------------------------------------------
    if (dictionary[ "SQL_MYSQL" ] == "yes")
        qmakeSql += "mysql";
    else if (dictionary[ "SQL_MYSQL" ] == "plugin")
        qmakeSqlPlugins += "mysql";

    if (dictionary[ "SQL_ODBC" ] == "yes")
        qmakeSql += "odbc";
    else if (dictionary[ "SQL_ODBC" ] == "plugin")
        qmakeSqlPlugins += "odbc";

    if (dictionary[ "SQL_OCI" ] == "yes")
        qmakeSql += "oci";
    else if (dictionary[ "SQL_OCI" ] == "plugin")
        qmakeSqlPlugins += "oci";

    if (dictionary[ "SQL_PSQL" ] == "yes")
        qmakeSql += "psql";
    else if (dictionary[ "SQL_PSQL" ] == "plugin")
        qmakeSqlPlugins += "psql";

    if (dictionary[ "SQL_TDS" ] == "yes")
        qmakeSql += "tds";
    else if (dictionary[ "SQL_TDS" ] == "plugin")
        qmakeSqlPlugins += "tds";

    if (dictionary[ "SQL_DB2" ] == "yes")
        qmakeSql += "db2";
    else if (dictionary[ "SQL_DB2" ] == "plugin")
        qmakeSqlPlugins += "db2";

    if (dictionary[ "SQL_SQLITE" ] == "yes")
        qmakeSql += "sqlite";
    else if (dictionary[ "SQL_SQLITE" ] == "plugin")
        qmakeSqlPlugins += "sqlite";

    if (dictionary[ "SQL_SQLITE_LIB" ] == "system")
        qmakeConfig += "system-sqlite";

    if (dictionary[ "SQL_SQLITE2" ] == "yes")
        qmakeSql += "sqlite2";
    else if (dictionary[ "SQL_SQLITE2" ] == "plugin")
        qmakeSqlPlugins += "sqlite2";

    if (dictionary[ "SQL_IBASE" ] == "yes")
        qmakeSql += "ibase";
    else if (dictionary[ "SQL_IBASE" ] == "plugin")
        qmakeSqlPlugins += "ibase";

    // Other options ------------------------------------------------
    if (dictionary[ "BUILDALL" ] == "yes") {
        qtConfig += "build_all";
    }
    if (dictionary[ "FORCEDEBUGINFO" ] == "yes")
        qtConfig += "force_debug_info";
    qmakeConfig += dictionary[ "BUILD" ];
    dictionary[ "QMAKE_OUTDIR" ] = dictionary[ "BUILD" ];

    if (buildParts.isEmpty()) {
        buildParts = defaultBuildParts;

        if (dictionary["BUILDDEV"] == "yes")
            buildParts += "tests";
    }
    while (!nobuildParts.isEmpty())
        buildParts.removeAll(nobuildParts.takeFirst());
    if (!buildParts.contains("libs"))
        buildParts += "libs";
    buildParts.removeDuplicates();

    if (dictionary["MSVC_MP"] == "yes")
        qmakeConfig += "msvc_mp";

    if (dictionary[ "SHARED" ] == "yes") {
        QString version = dictionary[ "VERSION" ];
        if (!version.isEmpty()) {
            qmakeVars += "QMAKE_QT_VERSION_OVERRIDE = " + version.left(version.indexOf("."));
            version.remove(QLatin1Char('.'));
        }
        dictionary[ "QMAKE_OUTDIR" ] += "_shared";
    } else {
        dictionary[ "QMAKE_OUTDIR" ] += "_static";
    }

    if (dictionary[ "ACCESSIBILITY" ] == "yes")
        qtConfig += "accessibility";

    if (!qmakeLibs.isEmpty())
        qmakeVars += "LIBS           += " + formatPaths(qmakeLibs);

    if (!dictionary["QT_LFLAGS_SQLITE"].isEmpty())
        qmakeVars += "QT_LFLAGS_SQLITE += " + formatPath(dictionary["QT_LFLAGS_SQLITE"]);

    if (dictionary[ "OPENGL" ] == "yes")
        qtConfig += "opengl";

    if (dictionary["OPENGL_ES_CM"] == "yes") {
        qtConfig += "opengles1";
        qtConfig += "egl";
    }

    if (dictionary["OPENGL_ES_2"] == "yes") {
        qtConfig += "opengles2";
        qtConfig += "egl";
    }

    if (dictionary["OPENVG"] == "yes") {
        qtConfig += "openvg";
        qtConfig += "egl";
    }

    if (dictionary[ "OPENSSL" ] == "yes")
        qtConfig += "openssl";
    else if (dictionary[ "OPENSSL" ] == "linked")
        qtConfig += "openssl-linked";

    if (dictionary[ "DBUS" ] == "yes")
        qtConfig += "dbus";
    else if (dictionary[ "DBUS" ] == "linked")
        qtConfig += "dbus dbus-linked";

    if (dictionary[ "CETEST" ] == "yes")
        qtConfig += "cetest";

    // ### Vestige
    if (dictionary["AUDIO_BACKEND"] == "yes")
        qtConfig += "audio-backend";

    if (dictionary["DIRECTWRITE"] == "yes")
        qtConfig += "directwrite";

    if (dictionary[ "NATIVE_GESTURES" ] == "yes")
        qtConfig += "native-gestures";

    qtConfig += "qpa";

    if (dictionary["NIS"] == "yes")
        qtConfig += "nis";

    if (dictionary["QT_CUPS"] == "yes")
        qtConfig += "cups";

    if (dictionary["QT_ICONV"] == "yes")
        qtConfig += "iconv";
    else if (dictionary["QT_ICONV"] == "sun")
        qtConfig += "sun-libiconv";
    else if (dictionary["QT_ICONV"] == "gnu")
        qtConfig += "gnu-libiconv";

    if (dictionary["FONT_CONFIG"] == "yes") {
        qtConfig += "fontconfig";
        qmakeVars += "QMAKE_CFLAGS_FONTCONFIG =";
        qmakeVars += "QMAKE_LIBS_FONTCONFIG   = -lfreetype -lfontconfig";
    }

    if (dictionary["QT_GLIB"] == "yes")
        qtConfig += "glib";

    // We currently have no switch for QtConcurrent, so add it unconditionally.
    qtConfig += "concurrent";

    // ### Vestige
    if (dictionary[ "V8SNAPSHOT" ] == "yes")
        qtConfig += "v8snapshot";

    // Add config levels --------------------------------------------
    QStringList possible_configs = QStringList()
        << "minimal"
        << "small"
        << "medium"
        << "large"
        << "full";

    QString set_config = dictionary["QCONFIG"];
    if (possible_configs.contains(set_config)) {
        foreach (const QString &cfg, possible_configs) {
            qtConfig += (cfg + "-config");
            if (cfg == set_config)
                break;
        }
    }

    if (dictionary.contains("XQMAKESPEC") && (dictionary["QMAKESPEC"] != dictionary["XQMAKESPEC"])) {
            qmakeConfig += "cross_compile";
            dictionary["CROSS_COMPILE"] = "yes";
    }

    // Directories and settings for .qmake.cache --------------------

    if (dictionary.contains("XQMAKESPEC") && dictionary[ "XQMAKESPEC" ].startsWith("linux"))
        qtConfig += "rpath";

    qmakeVars += QString("OBJECTS_DIR     = ") + formatPath("tmp/obj/" + dictionary["QMAKE_OUTDIR"]);
    qmakeVars += QString("MOC_DIR         = ") + formatPath("tmp/moc/" + dictionary["QMAKE_OUTDIR"]);
    qmakeVars += QString("RCC_DIR         = ") + formatPath("tmp/rcc/" + dictionary["QMAKE_OUTDIR"]);

    if (!qmakeDefines.isEmpty())
        qmakeVars += QString("DEFINES        += ") + qmakeDefines.join(" ");
    if (!qmakeIncludes.isEmpty())
        qmakeVars += QString("INCLUDEPATH    += ") + formatPaths(qmakeIncludes);
    if (!opensslLibs.isEmpty())
        qmakeVars += opensslLibs;
    if (dictionary[ "OPENSSL" ] == "linked") {
        if (!opensslLibsDebug.isEmpty() || !opensslLibsRelease.isEmpty()) {
            if (opensslLibsDebug.isEmpty() || opensslLibsRelease.isEmpty()) {
                cout << "Error: either both or none of OPENSSL_LIBS_DEBUG/_RELEASE must be defined." << endl;
                exit(1);
            }
            qmakeVars += opensslLibsDebug;
            qmakeVars += opensslLibsRelease;
        } else if (opensslLibs.isEmpty()) {
            qmakeVars += QString("OPENSSL_LIBS    = -lssleay32 -llibeay32");
        }
        if (!opensslPath.isEmpty())
            qmakeVars += opensslPath;
    }
    if (dictionary[ "DBUS" ] != "no" && !dbusPath.isEmpty())
        qmakeVars += dbusPath;
    if (dictionary[ "SQL_MYSQL" ] != "no" && !mysqlPath.isEmpty())
        qmakeVars += mysqlPath;
    if (!psqlLibs.isEmpty())
        qmakeVars += QString("QT_LFLAGS_PSQL=") + psqlLibs.section("=", 1);
    if (!zlibLibs.isEmpty())
        qmakeVars += zlibLibs;

    {
        QStringList lflagsTDS;
        if (!sybase.isEmpty())
            lflagsTDS += QString("-L") + formatPath(sybase.section("=", 1) + "/lib");
        if (!sybaseLibs.isEmpty())
            lflagsTDS += sybaseLibs.section("=", 1);
        if (!lflagsTDS.isEmpty())
            qmakeVars += QString("QT_LFLAGS_TDS=") + lflagsTDS.join(" ");
    }

    if (!qmakeSql.isEmpty())
        qmakeVars += QString("sql-drivers    += ") + qmakeSql.join(" ");
    if (!qmakeSqlPlugins.isEmpty())
        qmakeVars += QString("sql-plugins    += ") + qmakeSqlPlugins.join(" ");
    if (!qmakeStyles.isEmpty())
        qmakeVars += QString("styles         += ") + qmakeStyles.join(" ");
    if (!qmakeStylePlugins.isEmpty())
        qmakeVars += QString("style-plugins  += ") + qmakeStylePlugins.join(" ");

    if (dictionary["QMAKESPEC"].endsWith("-g++")) {
        QString includepath = qgetenv("INCLUDE");
        bool hasSh = Environment::detectExecutable("sh.exe");
        QChar separator = (!includepath.contains(":\\") && hasSh ? QChar(':') : QChar(';'));
        qmakeVars += QString("TMPPATH            = $$quote($$(INCLUDE))");
        qmakeVars += QString("QMAKE_INCDIR_POST += $$split(TMPPATH,\"%1\")").arg(separator);
        qmakeVars += QString("TMPPATH            = $$quote($$(LIB))");
        qmakeVars += QString("QMAKE_LIBDIR_POST += $$split(TMPPATH,\"%1\")").arg(separator);
    }

    if (!dictionary[ "QMAKESPEC" ].length()) {
        cout << "Configure could not detect your compiler. QMAKESPEC must either" << endl
             << "be defined as an environment variable, or specified as an" << endl
             << "argument with -platform" << endl;
        dictionary[ "HELP" ] = "yes";

        QStringList winPlatforms;
        QDir mkspecsDir(sourcePath + "/mkspecs");
        const QFileInfoList &specsList = mkspecsDir.entryInfoList();
        for (int i = 0; i < specsList.size(); ++i) {
            const QFileInfo &fi = specsList.at(i);
            if (fi.fileName().left(5) == "win32") {
                winPlatforms += fi.fileName();
            }
        }
        cout << "Available platforms are: " << qPrintable(winPlatforms.join(", ")) << endl;
        dictionary[ "DONE" ] = "error";
    }
}

#if !defined(EVAL)
void Configure::generateCachefile()
{
    // Generate .qmake.cache
    QFile cacheFile(buildPath + "/.qmake.cache");
    if (cacheFile.open(QFile::WriteOnly | QFile::Text)) { // Truncates any existing file.
        QTextStream cacheStream(&cacheFile);

        cacheStream << "include($$PWD/mkspecs/qmodule.pri)" << endl;

        for (QStringList::Iterator var = qmakeVars.begin(); var != qmakeVars.end(); ++var) {
            cacheStream << (*var) << endl;
        }
        cacheStream << "CONFIG         += " << qmakeConfig.join(" ") << "no_private_qt_headers_warning QTDIR_build" << endl;

        cacheStream.flush();
        cacheFile.close();
    }

    // Generate qmodule.pri
    QFile moduleFile(dictionary[ "QT_BUILD_TREE" ] + "/mkspecs/qmodule.pri");
    if (moduleFile.open(QFile::WriteOnly | QFile::Text)) { // Truncates any existing file.
        QTextStream moduleStream(&moduleFile);

        moduleStream << "#paths" << endl;
        moduleStream << "QT_BUILD_TREE   = " << formatPath(dictionary["QT_BUILD_TREE"]) << endl;
        moduleStream << "QT_SOURCE_TREE  = " << formatPath(dictionary["QT_SOURCE_TREE"]) << endl;
        moduleStream << "QT_BUILD_PARTS += " << buildParts.join(" ") << endl << endl;

        if (dictionary["QT_EDITION"] != "QT_EDITION_OPENSOURCE")
            moduleStream << "DEFINES        *= QT_EDITION=QT_EDITION_DESKTOP" << endl;

        if (dictionary["CETEST"] == "yes") {
            moduleStream << "QT_CE_RAPI_INC  = " << formatPath(dictionary["QT_CE_RAPI_INC"]) << endl;
            moduleStream << "QT_CE_RAPI_LIB  = " << formatPath(dictionary["QT_CE_RAPI_LIB"]) << endl;
        }

        moduleStream << "#Qt for Windows CE c-runtime deployment" << endl
                     << "QT_CE_C_RUNTIME = " << formatPath(dictionary["CE_CRT"]) << endl;

        if (dictionary["CE_SIGNATURE"] != QLatin1String("no"))
            moduleStream << "DEFAULT_SIGNATURE=" << dictionary["CE_SIGNATURE"] << endl;

        // embedded
        if (!dictionary["KBD_DRIVERS"].isEmpty())
            moduleStream << "kbd-drivers += "<< dictionary["KBD_DRIVERS"]<<endl;
        if (!dictionary["GFX_DRIVERS"].isEmpty())
            moduleStream << "gfx-drivers += "<< dictionary["GFX_DRIVERS"]<<endl;
        if (!dictionary["MOUSE_DRIVERS"].isEmpty())
            moduleStream << "mouse-drivers += "<< dictionary["MOUSE_DRIVERS"]<<endl;
        if (!dictionary["DECORATIONS"].isEmpty())
            moduleStream << "decorations += "<<dictionary["DECORATIONS"]<<endl;

        moduleStream << "CONFIG += create_prl link_prl";
        if (dictionary[ "SSE2" ] == "yes")
            moduleStream << " sse2";
        if (dictionary[ "SSE3" ] == "yes")
            moduleStream << " sse3";
        if (dictionary[ "SSSE3" ] == "yes")
            moduleStream << " ssse3";
        if (dictionary[ "SSE4_1" ] == "yes")
            moduleStream << " sse4_1";
        if (dictionary[ "SSE4_2" ] == "yes")
            moduleStream << " sse4_2";
        if (dictionary[ "AVX" ] == "yes")
            moduleStream << " avx";
        if (dictionary[ "AVX2" ] == "yes")
            moduleStream << " avx2";
        if (dictionary[ "IWMMXT" ] == "yes")
            moduleStream << " iwmmxt";
        if (dictionary[ "NEON" ] == "yes")
            moduleStream << " neon";
        if (dictionary[ "LARGE_FILE" ] == "yes")
            moduleStream << " largefile";
        moduleStream << endl;

        moduleStream.flush();
        moduleFile.close();
    }
}

struct ArchData {
    const char *qmakespec;
    const char *key;
    const char *subarchKey;
    const char *type;
    ArchData() {}
    ArchData(const char *t, const char *qm, const char *k, const char *sak)
        : qmakespec(qm), key(k), subarchKey(sak), type(t)
    {}
};

/*
    Runs qmake on config.tests/arch/arch.pro, which will detect the target arch
    for the compiler we are using
*/
void Configure::detectArch()
{
    QString oldpwd = QDir::currentPath();

    QString newpwd = QString("%1/config.tests/arch").arg(buildPath);
    if (!QDir().exists(newpwd) && !QDir().mkpath(newpwd)) {
        cout << "Failed to create directory " << qPrintable(QDir::toNativeSeparators(newpwd)) << endl;
        dictionary["DONE"] = "error";
        return;
    }
    if (!QDir::setCurrent(newpwd)) {
        cout << "Failed to change working directory to " << qPrintable(QDir::toNativeSeparators(newpwd)) << endl;
        dictionary["DONE"] = "error";
        return;
    }

    QVector<ArchData> qmakespecs;
    if (dictionary.contains("XQMAKESPEC"))
        qmakespecs << ArchData("target", "XQMAKESPEC", "QT_ARCH", "QT_CPU_FEATURES");
    qmakespecs << ArchData("host", "QMAKESPEC", "QT_HOST_ARCH", "QT_HOST_CPU_FEATURES");

    for (int i = 0; i < qmakespecs.count(); ++i) {
        const ArchData &data = qmakespecs.at(i);
        QString qmakespec = dictionary.value(data.qmakespec);
        QString key = data.key;
        QString subarchKey = data.subarchKey;

        // run qmake
        QString command = QString("%1 -spec %2 %3 2>&1")
            .arg(QDir::toNativeSeparators(buildPath + "/bin/qmake.exe"),
                 QDir::toNativeSeparators(qmakespec),
                 QDir::toNativeSeparators(sourcePath + "/config.tests/arch/arch.pro"));
        Environment::execute(command);

        // compile
        command = dictionary[ "MAKE" ];
        if (command.contains("nmake"))
            command += " /NOLOGO";
        command += " -s";
        Environment::execute(command);

        // find the executable that was generated
        QFile exe("arch.exe");
        if (!exe.open(QFile::ReadOnly)) { // no Text, this is binary
            exe.setFileName("arch");
            if (!exe.open(QFile::ReadOnly)) {
                cout << "Could not find output file: " << qPrintable(exe.errorString()) << endl;
                dictionary["DONE"] = "error";
                return;
            }
        }
        QByteArray exeContents = exe.readAll();
        exe.close();

        static const char archMagic[] = "==Qt=magic=Qt== Architecture:";
        int magicPos = exeContents.indexOf(archMagic);
        if (magicPos == -1) {
            cout << "Internal error, could not find the architecture of the "
                 << data.type << " executable" << endl;
            dictionary["DONE"] = "error";
            return;
        }
        //cout << "Found magic at offset 0x" << hex << magicPos << endl;

        // the conversion from QByteArray will stop at the ending NUL anyway
        QString arch = QString::fromLatin1(exeContents.constData() + magicPos
                                           + sizeof(archMagic) - 1);
        dictionary[key] = arch;

        static const char subarchMagic[] = "==Qt=magic=Qt== Sub-architecture:";
        magicPos = exeContents.indexOf(subarchMagic);
        if (magicPos == -1) {
            cout << "Internal error, could not find the sub-architecture of the "
                 << data.type << " executable" << endl;
            dictionary["DONE"] = "error";
            return;
        }

        QString subarch = QString::fromLatin1(exeContents.constData() + magicPos
                                              + sizeof(subarchMagic) - 1);
        dictionary[subarchKey] = subarch;

        //cout << "Detected arch '" << qPrintable(arch) << "'\n";
        //cout << "Detected sub-arch '" << qPrintable(subarch) << "'\n";

        // clean up
        Environment::execute(command + " distclean");
    }

    if (!dictionary.contains("QT_HOST_ARCH"))
        dictionary["QT_HOST_ARCH"] = "unknown";
    if (!dictionary.contains("QT_ARCH")) {
        dictionary["QT_ARCH"] = dictionary["QT_HOST_ARCH"];
        dictionary["QT_CPU_FEATURES"] = dictionary["QT_HOST_CPU_FEATURES"];
    }

    QDir::setCurrent(oldpwd);
}

bool Configure::tryCompileProject(const QString &projectPath, const QString &extraOptions)
{
    QString oldpwd = QDir::currentPath();

    QString newpwd = QString("%1/config.tests/%2").arg(buildPath, projectPath);
    if (!QDir().exists(newpwd) && !QDir().mkpath(newpwd)) {
        cout << "Failed to create directory " << qPrintable(QDir::toNativeSeparators(newpwd)) << endl;
        dictionary["DONE"] = "error";
        return false;
    }
    if (!QDir::setCurrent(newpwd)) {
        cout << "Failed to change working directory to " << qPrintable(QDir::toNativeSeparators(newpwd)) << endl;
        dictionary["DONE"] = "error";
        return false;
    }

    // run qmake
    QString command = QString("%1 %2 %3 2>&1")
        .arg(QDir::toNativeSeparators(buildPath + "/bin/qmake.exe"),
             QDir::toNativeSeparators(sourcePath + "/config.tests/" + projectPath),
             extraOptions);
    int code = 0;
    QString output = Environment::execute(command, &code);
    //cout << output << endl;

    if (code == 0) {
        // compile
        command = dictionary[ "MAKE" ];
        if (command.contains("nmake"))
            command += " /NOLOGO";
        command += " -s 2>&1";
        output = Environment::execute(command, &code);
        //cout << output << endl;

        // clean up
        Environment::execute(command + " distclean 2>&1");
    }

    QDir::setCurrent(oldpwd);
    return code == 0;
}

void Configure::generateQConfigPri()
{
    // Generate qconfig.pri
    QFile configFile(dictionary[ "QT_BUILD_TREE" ] + "/mkspecs/qconfig.pri");
    if (configFile.open(QFile::WriteOnly | QFile::Text)) { // Truncates any existing file.
        QTextStream configStream(&configFile);

        configStream << "CONFIG+= ";
        configStream << dictionary[ "BUILD" ];

        if (dictionary[ "LTCG" ] == "yes")
            configStream << " ltcg";
        if (dictionary[ "RTTI" ] == "yes")
            configStream << " rtti";
        if (dictionary["INCREDIBUILD_XGE"] == "yes")
            configStream << " incredibuild_xge";
        if (dictionary["PLUGIN_MANIFESTS"] == "no")
            configStream << " no_plugin_manifest";
        if (dictionary["CROSS_COMPILE"] == "yes")
            configStream << " cross_compile";

        if (dictionary["DIRECTWRITE"] == "yes")
            configStream << "directwrite";

        // ### For compatibility only, should be removed later.
        configStream << " qpa";

        configStream << endl;
        configStream << "QT_ARCH = " << dictionary["QT_ARCH"] << endl;
        configStream << "QT_HOST_ARCH = " << dictionary["QT_HOST_ARCH"] << endl;
        configStream << "QT_CPU_FEATURES = " << dictionary["QT_CPU_FEATURES"] << endl;
        configStream << "QT_HOST_CPU_FEATURES = " << dictionary["QT_HOST_CPU_FEATURES"] << endl;
        if (!dictionary["XQMAKESPEC"].isEmpty() && !dictionary["XQMAKESPEC"].startsWith("wince")) {
            // FIXME: add detection
            configStream << "QMAKE_DEFAULT_LIBDIRS = /lib /usr/lib" << endl;
            configStream << "QMAKE_DEFAULT_INCDIRS = /usr/include /usr/local/include" << endl;
        }
        if (dictionary["QT_EDITION"].contains("OPENSOURCE"))
            configStream << "QT_EDITION = " << QLatin1String("OpenSource") << endl;
        else
            configStream << "QT_EDITION = " << dictionary["EDITION"] << endl;
        configStream << "QT_CONFIG += " << qtConfig.join(" ") << endl;

        configStream << "#versioning " << endl
                     << "QT_VERSION = " << dictionary["VERSION"] << endl
                     << "QT_MAJOR_VERSION = " << dictionary["VERSION_MAJOR"] << endl
                     << "QT_MINOR_VERSION = " << dictionary["VERSION_MINOR"] << endl
                     << "QT_PATCH_VERSION = " << dictionary["VERSION_PATCH"] << endl;

        if (!dictionary["CFG_SYSROOT"].isEmpty() && dictionary["CFG_GCC_SYSROOT"] == "yes") {
            configStream << endl
                         << "# sysroot" << endl
                         << "!host_build {" << endl
                         << "    QMAKE_CFLAGS    += --sysroot=$$[QT_SYSROOT]" << endl
                         << "    QMAKE_CXXFLAGS  += --sysroot=$$[QT_SYSROOT]" << endl
                         << "    QMAKE_LFLAGS    += --sysroot=$$[QT_SYSROOT]" << endl
                         << "}" << endl;
        }

        if (!dictionary["QMAKE_RPATHDIR"].isEmpty())
            configStream << "QMAKE_RPATHDIR += " << formatPath(dictionary["QMAKE_RPATHDIR"]) << endl;

        if (!dictionary["QT_LIBINFIX"].isEmpty())
            configStream << "QT_LIBINFIX = " << dictionary["QT_LIBINFIX"] << endl;

        if (!dictionary["QT_NAMESPACE"].isEmpty())
            configStream << "#namespaces" << endl << "QT_NAMESPACE = " << dictionary["QT_NAMESPACE"] << endl;

        if (dictionary.value(QStringLiteral("OPENGL_ES_2")) == QStringLiteral("yes")) {
            const QString angleDir = dictionary.value(QStringLiteral("ANGLE_DIR"));
            if (!angleDir.isEmpty()) {
                configStream
                    << "QMAKE_INCDIR_OPENGL_ES2 = " << angleDir << "/include\n"
                    << "QMAKE_LIBDIR_OPENGL_ES2_DEBUG = " << angleDir << "/lib/Debug\n"
                    << "QMAKE_LIBDIR_OPENGL_ES2_RELEASE = " << angleDir << "/lib/Release\n";
            }
        }

        configStream.flush();
        configFile.close();
    }
}
#endif

QString Configure::addDefine(QString def)
{
    QString result, defNeg, defD = def;

    defD.replace(QRegExp("=.*"), "");
    def.replace(QRegExp("="), " ");

    if (def.startsWith("QT_NO_")) {
        defNeg = defD;
        defNeg.replace("QT_NO_", "QT_");
    } else if (def.startsWith("QT_")) {
        defNeg = defD;
        defNeg.replace("QT_", "QT_NO_");
    }

    if (defNeg.isEmpty()) {
        result = "#ifndef $DEFD\n"
                 "# define $DEF\n"
                 "#endif\n\n";
    } else {
        result = "#if defined($DEFD) && defined($DEFNEG)\n"
                 "# undef $DEFD\n"
                 "#elif !defined($DEFD)\n"
                 "# define $DEF\n"
                 "#endif\n\n";
    }
    result.replace("$DEFNEG", defNeg);
    result.replace("$DEFD", defD);
    result.replace("$DEF", def);
    return result;
}

#if !defined(EVAL)
bool Configure::copySpec(const char *name, const char *pfx, const QString &spec)
{
    // "Link" configured mkspec to default directory, but remove the old one first, if there is any
    QString defSpec = buildPath + "/mkspecs/" + name;
    QFileInfo defSpecInfo(defSpec);
    if (defSpecInfo.exists()) {
        if (!Environment::rmdir(defSpec)) {
            cout << "Couldn't update default " << pfx << "mkspec! Are files in " << qPrintable(defSpec) << " read-only?" << endl;
            dictionary["DONE"] = "error";
            return false;
        }
    }

    QDir::current().mkpath(defSpec);
    QFile qfile(defSpec + "/qmake.conf");
    if (qfile.open(QFile::WriteOnly | QFile::Text)) {
        QTextStream fileStream;
        fileStream.setDevice(&qfile);
        QString srcSpec = buildPath + "/mkspecs/" + spec; // We copied it to the build dir
        fileStream << "QMAKESPEC_ORIGINAL = " << formatPath(srcSpec) << endl;
        fileStream << "include(" << formatPath(QDir(defSpec).relativeFilePath(srcSpec + "/qmake.conf")) << ")" << endl;
        qfile.close();
    }
    if (qfile.error() != QFile::NoError) {
        cout << "Couldn't update default " << pfx << "mkspec: " << qPrintable(qfile.errorString()) << endl;
        dictionary["DONE"] = "error";
        return false;
    }
    return true;
}

void Configure::generateConfigfiles()
{
    QDir(buildPath).mkpath("src/corelib/global");
    QString outName(buildPath + "/src/corelib/global/qconfig.h");
    QTemporaryFile tmpFile;
    QTextStream tmpStream;

    if (tmpFile.open()) {
        tmpStream.setDevice(&tmpFile);

        if (dictionary[ "QCONFIG" ] == "full") {
            tmpStream << "/* Everything */" << endl;
        } else {
            QString configName("qconfig-" + dictionary[ "QCONFIG" ] + ".h");
            tmpStream << "/* Copied from " << configName << "*/" << endl;
            tmpStream << "#ifndef QT_BOOTSTRAPPED" << endl;
            QFile inFile(sourcePath + "/src/corelib/global/" + configName);
            if (inFile.open(QFile::ReadOnly)) {
                QByteArray buffer = inFile.readAll();
                tmpFile.write(buffer.constData(), buffer.size());
                inFile.close();
            }
            tmpStream << "#endif // QT_BOOTSTRAPPED" << endl;
        }
        tmpStream << endl;

        if (dictionary[ "SHARED" ] == "no") {
            tmpStream << "/* Qt was configured for a static build */" << endl
                      << "#if !defined(QT_SHARED) && !defined(QT_STATIC)" << endl
                      << "# define QT_STATIC" << endl
                      << "#endif" << endl
                      << endl;
        }
        tmpStream << "/* License information */" << endl;
        tmpStream << "#define QT_PRODUCT_LICENSEE \"" << licenseInfo[ "LICENSEE" ] << "\"" << endl;
        tmpStream << "#define QT_PRODUCT_LICENSE \"" << dictionary[ "EDITION" ] << "\"" << endl;
        tmpStream << endl;
        tmpStream << "// Qt Edition" << endl;
        tmpStream << "#ifndef QT_EDITION" << endl;
        tmpStream << "#  define QT_EDITION " << dictionary["QT_EDITION"] << endl;
        tmpStream << "#endif" << endl;
        tmpStream << endl;
        if (dictionary["BUILDDEV"] == "yes") {
            dictionary["QMAKE_INTERNAL"] = "yes";
            tmpStream << "/* Used for example to export symbols for the certain autotests*/" << endl;
            tmpStream << "#define QT_BUILD_INTERNAL" << endl;
            tmpStream << endl;
        }

        tmpStream << endl << "// Compiler sub-arch support" << endl;
        if (dictionary[ "SSE2" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_SSE2" << endl;
        if (dictionary[ "SSE3" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_SSE3" << endl;
        if (dictionary[ "SSSE3" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_SSSE3" << endl;
        if (dictionary[ "SSE4_1" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_SSE4_1" << endl;
        if (dictionary[ "SSE4_2" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_SSE4_2" << endl;
        if (dictionary[ "AVX" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_AVX" << endl;
        if (dictionary[ "AVX2" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_AVX2" << endl;
        if (dictionary[ "IWMMXT" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_IWMMXT" << endl;
        if (dictionary[ "NEON" ] == "yes")
            tmpStream << "#define QT_COMPILER_SUPPORTS_NEON" << endl;


        tmpStream << endl << "// Compile time features" << endl;

        QStringList qconfigList;
        if (dictionary["STYLE_WINDOWS"] != "yes")     qconfigList += "QT_NO_STYLE_WINDOWS";
        if (dictionary["STYLE_PLASTIQUE"] != "yes")   qconfigList += "QT_NO_STYLE_PLASTIQUE";
        if (dictionary["STYLE_CLEANLOOKS"] != "yes")   qconfigList += "QT_NO_STYLE_CLEANLOOKS";
        if (dictionary["STYLE_WINDOWSXP"] != "yes" && dictionary["STYLE_WINDOWSVISTA"] != "yes")
            qconfigList += "QT_NO_STYLE_WINDOWSXP";
        if (dictionary["STYLE_WINDOWSVISTA"] != "yes")   qconfigList += "QT_NO_STYLE_WINDOWSVISTA";
        if (dictionary["STYLE_MOTIF"] != "yes")       qconfigList += "QT_NO_STYLE_MOTIF";
        if (dictionary["STYLE_CDE"] != "yes")         qconfigList += "QT_NO_STYLE_CDE";

        // ### We still need the QT_NO_STYLE_S60 define for compiling Qt. Remove later!
        qconfigList += "QT_NO_STYLE_S60";

        if (dictionary["STYLE_WINDOWSCE"] != "yes")   qconfigList += "QT_NO_STYLE_WINDOWSCE";
        if (dictionary["STYLE_WINDOWSMOBILE"] != "yes")   qconfigList += "QT_NO_STYLE_WINDOWSMOBILE";
        if (dictionary["STYLE_GTK"] != "yes")         qconfigList += "QT_NO_STYLE_GTK";

        if (dictionary["GIF"] == "yes")              qconfigList += "QT_BUILTIN_GIF_READER=1";
        if (dictionary["PNG"] != "yes")              qconfigList += "QT_NO_IMAGEFORMAT_PNG";
        if (dictionary["JPEG"] != "yes")             qconfigList += "QT_NO_IMAGEFORMAT_JPEG";
        if (dictionary["ZLIB"] == "no") {
            qconfigList += "QT_NO_ZLIB";
            qconfigList += "QT_NO_COMPRESS";
        }

        if (dictionary["ACCESSIBILITY"] == "no")     qconfigList += "QT_NO_ACCESSIBILITY";
        if (dictionary["WIDGETS"] == "no")           qconfigList += "QT_NO_WIDGETS";
        if (dictionary["OPENGL"] == "no")            qconfigList += "QT_NO_OPENGL";
        if (dictionary["OPENVG"] == "no")            qconfigList += "QT_NO_OPENVG";
        if (dictionary["OPENSSL"] == "no") {
            qconfigList += "QT_NO_OPENSSL";
            qconfigList += "QT_NO_SSL";
        }
        if (dictionary["OPENSSL"] == "linked")       qconfigList += "QT_LINKED_OPENSSL";
        if (dictionary["DBUS"] == "no")              qconfigList += "QT_NO_DBUS";
        if (dictionary["QML_DEBUG"] == "no")         qconfigList += "QT_QML_NO_DEBUGGER";
        if (dictionary["FREETYPE"] == "no")          qconfigList += "QT_NO_FREETYPE";
        if (dictionary["NATIVE_GESTURES"] == "no")   qconfigList += "QT_NO_NATIVE_GESTURES";

        if (dictionary["OPENGL_ES_CM"] == "yes" ||
           dictionary["OPENGL_ES_2"]  == "yes")     qconfigList += "QT_OPENGL_ES";

        if (dictionary["OPENGL_ES_CM"] == "yes")     qconfigList += "QT_OPENGL_ES_1";
        if (dictionary["OPENGL_ES_2"]  == "yes")     qconfigList += "QT_OPENGL_ES_2";
        if (dictionary["SQL_MYSQL"] == "yes")        qconfigList += "QT_SQL_MYSQL";
        if (dictionary["SQL_ODBC"] == "yes")         qconfigList += "QT_SQL_ODBC";
        if (dictionary["SQL_OCI"] == "yes")          qconfigList += "QT_SQL_OCI";
        if (dictionary["SQL_PSQL"] == "yes")         qconfigList += "QT_SQL_PSQL";
        if (dictionary["SQL_TDS"] == "yes")          qconfigList += "QT_SQL_TDS";
        if (dictionary["SQL_DB2"] == "yes")          qconfigList += "QT_SQL_DB2";
        if (dictionary["SQL_SQLITE"] == "yes")       qconfigList += "QT_SQL_SQLITE";
        if (dictionary["SQL_SQLITE2"] == "yes")      qconfigList += "QT_SQL_SQLITE2";
        if (dictionary["SQL_IBASE"] == "yes")        qconfigList += "QT_SQL_IBASE";

        if (dictionary["POSIX_IPC"] == "yes")        qconfigList += "QT_POSIX_IPC";

        if (dictionary["FONT_CONFIG"] == "no")       qconfigList += "QT_NO_FONTCONFIG";

        if (dictionary["NIS"] == "yes")
            qconfigList += "QT_NIS";
        else
            qconfigList += "QT_NO_NIS";

        if (dictionary["LARGE_FILE"] == "yes")       qconfigList += "QT_LARGEFILE_SUPPORT=64";
        if (dictionary["QT_CUPS"] == "no")           qconfigList += "QT_NO_CUPS";
        if (dictionary["QT_ICONV"] == "no")          qconfigList += "QT_NO_ICONV";
        if (dictionary["QT_GLIB"] == "no")           qconfigList += "QT_NO_GLIB";
        if (dictionary["QT_INOTIFY"] == "no")        qconfigList += "QT_NO_INOTIFY";

        qconfigList.sort();
        for (int i = 0; i < qconfigList.count(); ++i)
            tmpStream << addDefine(qconfigList.at(i));

        tmpStream<<"#define QT_QPA_DEFAULT_PLATFORM_NAME \"" << qpaPlatformName() << "\""<<endl;

        tmpStream.flush();
        tmpFile.flush();

        // Replace old qconfig.h with new one
        ::SetFileAttributes((wchar_t*)outName.utf16(), FILE_ATTRIBUTE_NORMAL);
        QFile::remove(outName);
        tmpFile.copy(outName);
        tmpFile.close();
    }

    QTemporaryFile tmpFile3;
    if (tmpFile3.open()) {
        tmpStream.setDevice(&tmpFile3);
        tmpStream << "/* Evaluation license key */" << endl
                  << "static const volatile char qt_eval_key_data              [512 + 12] = \"qt_qevalkey=" << licenseInfo["LICENSEKEYEXT"] << "\";" << endl;

        tmpStream.flush();
        tmpFile3.flush();

        outName = buildPath + "/src/corelib/global/qconfig_eval.cpp";
        ::SetFileAttributes((wchar_t*)outName.utf16(), FILE_ATTRIBUTE_NORMAL);
        QFile::remove(outName);

        if (dictionary["EDITION"] == "Evaluation" || qmakeDefines.contains("QT_EVAL"))
            tmpFile3.copy(outName);
        tmpFile3.close();
    }
}
#endif

#if !defined(EVAL)
void Configure::displayConfig()
{
    fstream sout;
    sout.open(QString(buildPath + "/config.summary").toLocal8Bit().constData(),
              ios::in | ios::out | ios::trunc);

    // Give some feedback
    sout << "Environment:" << endl;
    QString env = QString::fromLocal8Bit(getenv("INCLUDE")).replace(QRegExp("[;,]"), "\r\n      ");
    if (env.isEmpty())
        env = "Unset";
    sout << "    INCLUDE=\r\n      " << env << endl;
    env = QString::fromLocal8Bit(getenv("LIB")).replace(QRegExp("[;,]"), "\r\n      ");
    if (env.isEmpty())
        env = "Unset";
    sout << "    LIB=\r\n      " << env << endl;
    env = QString::fromLocal8Bit(getenv("PATH")).replace(QRegExp("[;,]"), "\r\n      ");
    if (env.isEmpty())
        env = "Unset";
    sout << "    PATH=\r\n      " << env << endl;

    if (dictionary[QStringLiteral("EDITION")] != QStringLiteral("OpenSource")) {
        QString l1 = licenseInfo[ "LICENSEE" ];
        QString l2 = licenseInfo[ "LICENSEID" ];
        QString l3 = dictionary["EDITION"] + ' ' + "Edition";
        QString l4 = licenseInfo[ "EXPIRYDATE" ];
        sout << "Licensee...................." << (l1.isNull() ? "" : l1) << endl;
        sout << "License ID.................." << (l2.isNull() ? "" : l2) << endl;
        sout << "Product license............." << (l3.isNull() ? "" : l3) << endl;
        sout << "Expiry Date................." << (l4.isNull() ? "" : l4) << endl << endl;
    }

    sout << "Configuration:" << endl;
    sout << "    " << qmakeConfig.join("\r\n    ") << endl;
    sout << "Qt Configuration:" << endl;
    sout << "    " << qtConfig.join("\r\n    ") << endl;
    sout << endl;

    if (dictionary.contains("XQMAKESPEC"))
        sout << "QMAKESPEC..................." << dictionary[ "XQMAKESPEC" ] << " (" << dictionary["QMAKESPEC_FROM"] << ")" << endl;
    else
        sout << "QMAKESPEC..................." << dictionary[ "QMAKESPEC" ] << " (" << dictionary["QMAKESPEC_FROM"] << ")" << endl;
    sout << "Architecture................" << dictionary["QT_ARCH"]
         << ", features:" << dictionary["QT_CPU_FEATURES"] << endl;
    sout << "Host Architecture..........." << dictionary["QT_HOST_ARCH"]
         << ", features:" << dictionary["QT_HOST_CPU_FEATURES"]  << endl;
    sout << "Maketool...................." << dictionary[ "MAKE" ] << endl;
    if (dictionary[ "BUILDALL" ] == "yes") {
        sout << "Debug build................." << "yes (combined)" << endl;
        sout << "Default build..............." << dictionary[ "BUILD" ] << endl;
    } else {
        sout << "Debug......................." << (dictionary[ "BUILD" ] == "debug" ? "yes" : "no") << endl;
    }
    if (dictionary[ "BUILD" ] == "release" || dictionary[ "BUILDALL" ] == "yes")
        sout << "Force debug info............" << dictionary[ "FORCEDEBUGINFO" ] << endl;
    sout << "Link Time Code Generation..." << dictionary[ "LTCG" ] << endl;
    sout << "Accessibility support......." << dictionary[ "ACCESSIBILITY" ] << endl;
    sout << "RTTI support................" << dictionary[ "RTTI" ] << endl;
    sout << "SSE2 support................" << dictionary[ "SSE2" ] << endl;
    sout << "SSE3 support................" << dictionary[ "SSE3" ] << endl;
    sout << "SSSE3 support..............." << dictionary[ "SSSE3" ] << endl;
    sout << "SSE4.1 support.............." << dictionary[ "SSE4_1" ] << endl;
    sout << "SSE4.2 support.............." << dictionary[ "SSE4_2" ] << endl;
    sout << "AVX support................." << dictionary[ "AVX" ] << endl;
    sout << "AVX2 support................" << dictionary[ "AVX2" ] << endl;
    sout << "NEON support................" << dictionary[ "NEON" ] << endl;
    sout << "IWMMXT support.............." << dictionary[ "IWMMXT" ] << endl;
    sout << "OpenGL support.............." << dictionary[ "OPENGL" ] << endl;
    sout << "Large File support.........." << dictionary[ "LARGE_FILE" ] << endl;
    sout << "NIS support................." << dictionary[ "NIS" ] << endl;
    sout << "Iconv support..............." << dictionary[ "QT_ICONV" ] << endl;
    sout << "Glib support................" << dictionary[ "QT_GLIB" ] << endl;
    sout << "CUPS support................" << dictionary[ "QT_CUPS" ] << endl;
    if (dictionary.value(QStringLiteral("OPENGL_ES_2")) == QStringLiteral("yes")) {
        const QString angleDir = dictionary.value(QStringLiteral("ANGLE_DIR"));
        if (!angleDir.isEmpty())
            sout << "ANGLE......................." << QDir::toNativeSeparators(angleDir) << endl;
    }
    sout << "OpenVG support.............." << dictionary[ "OPENVG" ] << endl;
    sout << "OpenSSL support............." << dictionary[ "OPENSSL" ] << endl;
    sout << "QtDBus support.............." << dictionary[ "DBUS" ] << endl;
    sout << "QtWidgets module support...." << dictionary[ "WIDGETS" ] << endl;
    sout << "QML debugging..............." << dictionary[ "QML_DEBUG" ] << endl;
    sout << "DirectWrite support........." << dictionary[ "DIRECTWRITE" ] << endl << endl;

    sout << "Third Party Libraries:" << endl;
    sout << "    ZLIB support............" << dictionary[ "ZLIB" ] << endl;
    sout << "    GIF support............." << dictionary[ "GIF" ] << endl;
    sout << "    JPEG support............" << dictionary[ "JPEG" ] << endl;
    sout << "    PNG support............." << dictionary[ "PNG" ] << endl;
    sout << "    FreeType support........" << dictionary[ "FREETYPE" ] << endl << endl;
    sout << "    PCRE support............" << dictionary[ "PCRE" ] << endl;
    sout << "    ICU support............." << dictionary[ "ICU" ] << endl;

    sout << "Styles:" << endl;
    sout << "    Windows................." << dictionary[ "STYLE_WINDOWS" ] << endl;
    sout << "    Windows XP.............." << dictionary[ "STYLE_WINDOWSXP" ] << endl;
    sout << "    Windows Vista..........." << dictionary[ "STYLE_WINDOWSVISTA" ] << endl;
    sout << "    Plastique..............." << dictionary[ "STYLE_PLASTIQUE" ] << endl;
    sout << "    Cleanlooks.............." << dictionary[ "STYLE_CLEANLOOKS" ] << endl;
    sout << "    Motif..................." << dictionary[ "STYLE_MOTIF" ] << endl;
    sout << "    CDE....................." << dictionary[ "STYLE_CDE" ] << endl;
    sout << "    Windows CE.............." << dictionary[ "STYLE_WINDOWSCE" ] << endl;
    sout << "    Windows Mobile.........." << dictionary[ "STYLE_WINDOWSMOBILE" ] << endl << endl;

    sout << "Sql Drivers:" << endl;
    sout << "    ODBC...................." << dictionary[ "SQL_ODBC" ] << endl;
    sout << "    MySQL..................." << dictionary[ "SQL_MYSQL" ] << endl;
    sout << "    OCI....................." << dictionary[ "SQL_OCI" ] << endl;
    sout << "    PostgreSQL.............." << dictionary[ "SQL_PSQL" ] << endl;
    sout << "    TDS....................." << dictionary[ "SQL_TDS" ] << endl;
    sout << "    DB2....................." << dictionary[ "SQL_DB2" ] << endl;
    sout << "    SQLite.................." << dictionary[ "SQL_SQLITE" ] << " (" << dictionary[ "SQL_SQLITE_LIB" ] << ")" << endl;
    sout << "    SQLite2................." << dictionary[ "SQL_SQLITE2" ] << endl;
    sout << "    InterBase..............." << dictionary[ "SQL_IBASE" ] << endl << endl;

    sout << "Sources are in.............." << QDir::toNativeSeparators(dictionary["QT_SOURCE_TREE"]) << endl;
    sout << "Build is done in............" << QDir::toNativeSeparators(dictionary["QT_BUILD_TREE"]) << endl;
    sout << "Install prefix.............." << QDir::toNativeSeparators(dictionary["QT_INSTALL_PREFIX"]) << endl;
    sout << "Headers installed to........" << QDir::toNativeSeparators(dictionary["QT_INSTALL_HEADERS"]) << endl;
    sout << "Libraries installed to......" << QDir::toNativeSeparators(dictionary["QT_INSTALL_LIBS"]) << endl;
    sout << "Plugins installed to........" << QDir::toNativeSeparators(dictionary["QT_INSTALL_PLUGINS"]) << endl;
    sout << "Imports installed to........" << QDir::toNativeSeparators(dictionary["QT_INSTALL_IMPORTS"]) << endl;
    sout << "Binaries installed to......." << QDir::toNativeSeparators(dictionary["QT_INSTALL_BINS"]) << endl;
    sout << "Docs installed to..........." << QDir::toNativeSeparators(dictionary["QT_INSTALL_DOCS"]) << endl;
    sout << "Data installed to..........." << QDir::toNativeSeparators(dictionary["QT_INSTALL_DATA"]) << endl;
    sout << "Translations installed to..." << QDir::toNativeSeparators(dictionary["QT_INSTALL_TRANSLATIONS"]) << endl;
    sout << "Examples installed to......." << QDir::toNativeSeparators(dictionary["QT_INSTALL_EXAMPLES"]) << endl;
    sout << "Tests installed to.........." << QDir::toNativeSeparators(dictionary["QT_INSTALL_TESTS"]) << endl;

    if (dictionary.contains("XQMAKESPEC") && dictionary["XQMAKESPEC"].startsWith(QLatin1String("wince"))) {
        sout << "Using c runtime detection..." << dictionary[ "CE_CRT" ] << endl;
        sout << "Cetest support.............." << dictionary[ "CETEST" ] << endl;
        sout << "Signature..................." << dictionary[ "CE_SIGNATURE"] << endl << endl;
    }

    if (checkAvailability("INCREDIBUILD_XGE"))
        sout << "Using IncrediBuild XGE......" << dictionary["INCREDIBUILD_XGE"] << endl;
    if (!qmakeDefines.isEmpty()) {
        sout << "Defines.....................";
        for (QStringList::Iterator defs = qmakeDefines.begin(); defs != qmakeDefines.end(); ++defs)
            sout << (*defs) << " ";
        sout << endl;
    }
    if (!qmakeIncludes.isEmpty()) {
        sout << "Include paths...............";
        for (QStringList::Iterator incs = qmakeIncludes.begin(); incs != qmakeIncludes.end(); ++incs)
            sout << (*incs) << " ";
        sout << endl;
    }
    if (!qmakeLibs.isEmpty()) {
        sout << "Additional libraries........";
        for (QStringList::Iterator libs = qmakeLibs.begin(); libs != qmakeLibs.end(); ++libs)
            sout << (*libs) << " ";
        sout << endl;
    }
    if (dictionary[ "QMAKE_INTERNAL" ] == "yes") {
        sout << "Using internal configuration." << endl;
    }
    if (dictionary[ "SHARED" ] == "no") {
        sout << "WARNING: Using static linking will disable the use of plugins." << endl;
        sout << "         Make sure you compile ALL needed modules into the library." << endl;
    }
    if (dictionary[ "OPENSSL" ] == "linked") {
        if (!opensslLibsDebug.isEmpty() || !opensslLibsRelease.isEmpty()) {
            sout << "Using OpenSSL libraries:" << endl;
            sout << "   debug  : " << opensslLibsDebug << endl;
            sout << "   release: " << opensslLibsRelease << endl;
            sout << "   both   : " << opensslLibs << endl;
        } else if (opensslLibs.isEmpty()) {
            sout << "NOTE: When linking against OpenSSL, you can override the default" << endl;
            sout << "library names through OPENSSL_LIBS and optionally OPENSSL_LIBS_DEBUG/OPENSSL_LIBS_RELEASE" << endl;
            sout << "For example:" << endl;
            sout << "    configure -openssl-linked OPENSSL_LIBS=\"-lssleay32 -llibeay32\"" << endl;
        }
    }
    if (dictionary[ "ZLIB_FORCED" ] == "yes") {
        QString which_zlib = "supplied";
        if (dictionary[ "ZLIB" ] == "system")
            which_zlib = "system";

        sout << "NOTE: The -no-zlib option was supplied but is no longer supported." << endl
             << endl
             << "Qt now requires zlib support in all builds, so the -no-zlib" << endl
             << "option was ignored. Qt will be built using the " << which_zlib
             << "zlib" << endl;
    }
    if (dictionary["OBSOLETE_ARCH_ARG"] == "yes") {
        sout << endl
             << "NOTE: The -arch option is obsolete." << endl
             << endl
             << "Qt now detects the target and host architectures based on compiler" << endl
             << "output. Qt will be built using " << dictionary["QT_ARCH"] << " for the target architecture" << endl
             << "and " << dictionary["QT_HOST_ARCH"] << " for the host architecture (note that these two" << endl
             << "will be the same unless you are cross-compiling)." << endl
             << endl;
    }

    // display config.summary
    sout.seekg(0, ios::beg);
    while (sout.good()) {
        string str;
        getline(sout, str);
        cout << str << endl;
    }
}
#endif

#if !defined(EVAL)
void Configure::generateHeaders()
{
    if (dictionary["SYNCQT"] == "yes") {
        if (findFile("perl.exe")) {
            cout << "Running syncqt..." << endl;
            QStringList args;
            args += buildPath + "/bin/syncqt.bat";
            args += "-minimal";
            args += sourcePath;
            int retc = Environment::execute(args, QStringList(), QStringList());
            if (retc) {
                cout << "syncqt failed, return code " << retc << endl << endl;
                dictionary["DONE"] = "error";
            }
        } else {
            cout << "Perl not found in environment - cannot run syncqt." << endl;
            dictionary["DONE"] = "error";
        }
    }
}

void Configure::generateQConfigCpp()
{
    // if QT_INSTALL_* have not been specified on commandline, define them now from QT_INSTALL_PREFIX
    // if prefix is empty (WINCE), make all of them empty, if they aren't set
    bool qipempty = false;
    if (dictionary["QT_INSTALL_PREFIX"].isEmpty())
        qipempty = true;

    if (!dictionary["QT_INSTALL_DOCS"].size())
        dictionary["QT_INSTALL_DOCS"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/doc";
    if (!dictionary["QT_INSTALL_HEADERS"].size())
        dictionary["QT_INSTALL_HEADERS"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/include";
    if (!dictionary["QT_INSTALL_LIBS"].size())
        dictionary["QT_INSTALL_LIBS"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/lib";
    if (!dictionary["QT_INSTALL_BINS"].size())
        dictionary["QT_INSTALL_BINS"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/bin";
    if (!dictionary["QT_INSTALL_PLUGINS"].size())
        dictionary["QT_INSTALL_PLUGINS"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/plugins";
    if (!dictionary["QT_INSTALL_IMPORTS"].size())
        dictionary["QT_INSTALL_IMPORTS"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/imports";
    if (!dictionary["QT_INSTALL_DATA"].size())
        dictionary["QT_INSTALL_DATA"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"];
    if (!dictionary["QT_INSTALL_TRANSLATIONS"].size())
        dictionary["QT_INSTALL_TRANSLATIONS"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/translations";
    if (!dictionary["QT_INSTALL_EXAMPLES"].size())
        dictionary["QT_INSTALL_EXAMPLES"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/examples";
    if (!dictionary["QT_INSTALL_TESTS"].size())
        dictionary["QT_INSTALL_TESTS"] = qipempty ? "" : dictionary["QT_INSTALL_PREFIX"] + "/tests";

    bool haveHpx = false;
    if (dictionary["QT_HOST_PREFIX"].isEmpty())
        dictionary["QT_HOST_PREFIX"] = dictionary["QT_INSTALL_PREFIX"];
    else
        haveHpx = true;
    if (dictionary["QT_HOST_BINS"].isEmpty())
        dictionary["QT_HOST_BINS"] = haveHpx ? dictionary["QT_HOST_PREFIX"] + "/bin" : dictionary["QT_INSTALL_BINS"];
    if (dictionary["QT_HOST_DATA"].isEmpty())
        dictionary["QT_HOST_DATA"] = haveHpx ? dictionary["QT_HOST_PREFIX"] : dictionary["QT_INSTALL_DATA"];

    // Generate the new qconfig.cpp file
    QDir(buildPath).mkpath("src/corelib/global");
    const QString outName(buildPath + "/src/corelib/global/qconfig.cpp");

    QTemporaryFile tmpFile;
    if (tmpFile.open()) {
        QTextStream tmpStream(&tmpFile);
        tmpStream << "/* Licensed */" << endl
                  << "static const char qt_configure_licensee_str          [512 + 12] = \"qt_lcnsuser=" << licenseInfo["LICENSEE"] << "\";" << endl
                  << "static const char qt_configure_licensed_products_str [512 + 12] = \"qt_lcnsprod=" << dictionary["EDITION"] << "\";" << endl
                  << endl
                  << "/* Build date */" << endl
                  << "static const char qt_configure_installation          [11  + 12] = \"qt_instdate=" << QDate::currentDate().toString(Qt::ISODate) << "\";" << endl
                  << endl
                  << "static const char qt_configure_prefix_path_strs[][12 + 512] = {" << endl
                  << "    \"qt_prfxpath=" << formatPath(dictionary["QT_INSTALL_PREFIX"]) << "\"," << endl
                  << "    \"qt_docspath=" << formatPath(dictionary["QT_INSTALL_DOCS"]) << "\","  << endl
                  << "    \"qt_hdrspath=" << formatPath(dictionary["QT_INSTALL_HEADERS"]) << "\","  << endl
                  << "    \"qt_libspath=" << formatPath(dictionary["QT_INSTALL_LIBS"]) << "\","  << endl
                  << "    \"qt_binspath=" << formatPath(dictionary["QT_INSTALL_BINS"]) << "\","  << endl
                  << "    \"qt_plugpath=" << formatPath(dictionary["QT_INSTALL_PLUGINS"]) << "\","  << endl
                  << "    \"qt_impspath=" << formatPath(dictionary["QT_INSTALL_IMPORTS"]) << "\","  << endl
                  << "    \"qt_datapath=" << formatPath(dictionary["QT_INSTALL_DATA"]) << "\","  << endl
                  << "    \"qt_trnspath=" << formatPath(dictionary["QT_INSTALL_TRANSLATIONS"]) << "\"," << endl
                  << "    \"qt_xmplpath=" << formatPath(dictionary["QT_INSTALL_EXAMPLES"]) << "\","  << endl
                  << "    \"qt_tstspath=" << formatPath(dictionary["QT_INSTALL_TESTS"]) << "\","  << endl
                  << "#ifdef QT_BUILD_QMAKE" << endl
                  << "    \"qt_ssrtpath=" << formatPath(dictionary["CFG_SYSROOT"]) << "\"," << endl
                  << "    \"qt_hpfxpath=" << formatPath(dictionary["QT_HOST_PREFIX"]) << "\"," << endl
                  << "    \"qt_hbinpath=" << formatPath(dictionary["QT_HOST_BINS"]) << "\"," << endl
                  << "    \"qt_hdatpath=" << formatPath(dictionary["QT_HOST_DATA"]) << "\"," << endl
                  << "#endif" << endl
                  << "};" << endl;

        if ((platform() != WINDOWS) && (platform() != WINDOWS_CE))
            tmpStream << "static const char qt_configure_settings_path_str [256 + 12] = \"qt_stngpath=" << formatPath(dictionary["QT_INSTALL_SETTINGS"]) << "\";" << endl;

        tmpStream << endl
                  << "/* strlen( \"qt_lcnsxxxx\") == 12 */" << endl
                  << "#define QT_CONFIGURE_LICENSEE qt_configure_licensee_str + 12;" << endl
                  << "#define QT_CONFIGURE_LICENSED_PRODUCTS qt_configure_licensed_products_str + 12;" << endl;

        if ((platform() != WINDOWS) && (platform() != WINDOWS_CE))
            tmpStream << "#define QT_CONFIGURE_SETTINGS_PATH qt_configure_settings_path_str + 12;" << endl;

        tmpStream.flush();
        tmpFile.flush();

        // Replace old qconfig.cpp with new one
        ::SetFileAttributes((wchar_t*)outName.utf16(), FILE_ATTRIBUTE_NORMAL);
        QFile::remove(outName);
        tmpFile.copy(outName);
        tmpFile.close();
    }
}

void Configure::buildQmake()
{
    if (dictionary[ "BUILD_QMAKE" ] == "yes") {
        QStringList args;

        // Build qmake
        QString pwd = QDir::currentPath();
        if (!QDir(buildPath).mkpath("qmake")) {
            cout << "Cannot create qmake build dir." << endl;
            dictionary[ "DONE" ] = "error";
            return;
        }
        if (!QDir::setCurrent(buildPath + "/qmake")) {
            cout << "Cannot enter qmake build dir." << endl;
            dictionary[ "DONE" ] = "error";
            return;
        }

        QString makefile = "Makefile";
        {
            QFile out(makefile);
            if (out.open(QFile::WriteOnly | QFile::Text)) {
                QTextStream stream(&out);
                stream << "#AutoGenerated by configure.exe" << endl
                    << "BUILD_PATH = " << QDir::toNativeSeparators(buildPath) << endl
                    << "SOURCE_PATH = " << QDir::toNativeSeparators(sourcePath) << endl;
                stream << "QMAKESPEC = " << dictionary["QMAKESPEC"] << endl
                       << "QT_VERSION = " << dictionary["VERSION"] << endl;

                if (dictionary["EDITION"] == "OpenSource" ||
                    dictionary["QT_EDITION"].contains("OPENSOURCE"))
                    stream << "QMAKE_OPENSOURCE_EDITION = yes" << endl;
                stream << "\n\n";

                QFile in(sourcePath + "/qmake/" + dictionary["QMAKEMAKEFILE"]);
                if (in.open(QFile::ReadOnly | QFile::Text)) {
                    QString d = in.readAll();
                    //### need replaces (like configure.sh)? --Sam
                    stream << d << endl;
                }
                stream.flush();
                out.close();
            }
        }

        args += dictionary[ "MAKE" ];
        args += "-f";
        args += makefile;

        cout << "Creating qmake..." << endl;
        int exitCode = Environment::execute(args, QStringList(), QStringList());
        if (exitCode) {
            args.clear();
            args += dictionary[ "MAKE" ];
            args += "-f";
            args += makefile;
            args += "clean";
            exitCode = Environment::execute(args, QStringList(), QStringList());
            if (exitCode) {
                cout << "Cleaning qmake failed, return code " << exitCode << endl << endl;
                dictionary[ "DONE" ] = "error";
            } else {
                args.clear();
                args += dictionary[ "MAKE" ];
                args += "-f";
                args += makefile;
                exitCode = Environment::execute(args, QStringList(), QStringList());
                if (exitCode) {
                    cout << "Building qmake failed, return code " << exitCode << endl << endl;
                    dictionary[ "DONE" ] = "error";
                }
            }
        }
        QDir::setCurrent(pwd);
    }

    // Generate qt.conf
    QFile confFile(buildPath + "/bin/qt.conf");
    if (confFile.open(QFile::WriteOnly | QFile::Text)) { // Truncates any existing file.
        QTextStream confStream(&confFile);
        confStream << "[EffectivePaths]" << endl
                   << "Prefix=.." << endl;

        confStream.flush();
        confFile.close();
    }

    //create default mkspecs
    QString spec = dictionary.contains("XQMAKESPEC") ? dictionary["XQMAKESPEC"] : dictionary["QMAKESPEC"];
    if (!copySpec("default", "", spec)
        || !copySpec("default-host", "host ", dictionary["QMAKESPEC"])) {
        cout << "Error installing default mkspecs" << endl << endl;
        exit(EXIT_FAILURE);
    }

}
#endif

void Configure::findProjects(const QString& dirName)
{
    if (dictionary[ "PROCESS" ] != "no") {
        QDir dir(dirName);
        QString entryName;
        int makeListNumber;
        ProjectType qmakeTemplate;
        const QFileInfoList &list = dir.entryInfoList(QStringList(QLatin1String("*.pro")),
                                                      QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
        for (int i = 0; i < list.size(); ++i) {
            const QFileInfo &fi = list.at(i);
            if (fi.fileName() != "qmake.pro") {
                entryName = dirName + "/" + fi.fileName();
                if (fi.isDir()) {
                    findProjects(entryName);
                } else {
                    qmakeTemplate = projectType(fi.absoluteFilePath());
                    switch (qmakeTemplate) {
                    case Lib:
                    case Subdirs:
                        makeListNumber = 1;
                        break;
                    default:
                        makeListNumber = 2;
                        break;
                    }
                    makeList[makeListNumber].append(new MakeItem(sourceDir.relativeFilePath(fi.absolutePath()),
                                                    fi.fileName(),
                                                    "Makefile",
                                                    qmakeTemplate));
                }
            }

        }
    }
}

void Configure::appendMakeItem(int inList, const QString &item)
{
    QString dir;
    if (item != "src")
        dir = "/" + item;
    dir.prepend("/src");
    makeList[inList].append(new MakeItem(sourcePath + dir,
        item + ".pro", buildPath + dir + "/Makefile", Lib));
    if (dictionary[ "VCPROJFILES" ] == "yes") {
        makeList[inList].append(new MakeItem(sourcePath + dir,
            item + ".pro", buildPath + dir + "/" + item + ".vcproj", Lib));
    }
}

void Configure::generateMakefiles()
{
    if (dictionary[ "PROCESS" ] != "no") {
        QString spec = dictionary.contains("XQMAKESPEC") ? dictionary[ "XQMAKESPEC" ] : dictionary[ "QMAKESPEC" ];
        if (spec != "win32-msvc.net" && !spec.startsWith("win32-msvc2") && !spec.startsWith(QLatin1String("wince")))
            dictionary[ "VCPROJFILES" ] = "no";

        int i = 0;
        QString pwd = QDir::currentPath();
        if (dictionary["FAST"] != "yes") {
            QString dirName;
            bool generate = true;
            bool doDsp = (dictionary["VCPROJFILES"] == "yes");
            while (generate) {
                QString pwd = QDir::currentPath();
                QString dirPath = buildPath + dirName;
                QStringList args;

                args << buildPath + "/bin/qmake";

                if (doDsp) {
                    if (dictionary[ "DEPENDENCIES" ] == "no")
                        args << "-nodepend";
                    args << "-tp" <<  "vc";
                    doDsp = false; // DSP files will be done
                    printf("Generating Visual Studio project files...\n");
                } else {
                    printf("Generating Makefiles...\n");
                    generate = false; // Now Makefiles will be done
                }
                if (dictionary[ "PROCESS" ] == "full")
                    args << "-r";
                args << (sourcePath + "/qtbase.pro");
                args << "-o";
                args << buildPath;

                QDir::setCurrent(dirPath);
                if (int exitCode = Environment::execute(args, QStringList(), QStringList())) {
                    cout << "Qmake failed, return code " << exitCode  << endl << endl;
                    dictionary[ "DONE" ] = "error";
                }
            }
        } else {
            findProjects(sourcePath);
            for (i=0; i<3; i++) {
                for (int j=0; j<makeList[i].size(); ++j) {
                    MakeItem *it=makeList[i][j];
                    if (it->directory == "tools/configure")
                        continue; // don't overwrite our own Makefile

                    QString dirPath = it->directory + '/';
                    QString projectName = it->proFile;
                    QString makefileName = buildPath + "/" + dirPath + it->target;

                    // For shadowbuilds, we need to create the path first
                    QDir buildPathDir(buildPath);
                    if (sourcePath != buildPath && !buildPathDir.exists(dirPath))
                        buildPathDir.mkpath(dirPath);

                    QStringList args;

                    args << QDir::toNativeSeparators(buildPath + "/bin/qmake.exe");
                    args << sourcePath + "/" + dirPath + projectName;

                    cout << "For " << qPrintable(QDir::toNativeSeparators(dirPath + projectName)) << endl;
                    args << "-o";
                    args << it->target;

                    QDir::setCurrent(dirPath);

                    QFile file(makefileName);
                    if (!file.open(QFile::WriteOnly | QFile::Text)) {
                        printf("failed on dirPath=%s, makefile=%s\n",
                               qPrintable(QDir::toNativeSeparators(dirPath)),
                               qPrintable(QDir::toNativeSeparators(makefileName)));
                        continue;
                    }
                    QTextStream txt(&file);
                    txt << "all:\n";
                    txt << "\t" << args.join(" ") << "\n";
                    txt << "\t$(MAKE) -$(MAKEFLAGS) -f " << it->target << "\n";
                    txt << "first: all\n";
                    txt << "qmake: FORCE\n";
                    txt << "\t" << args.join(" ") << "\n";
                    txt << "FORCE:\n";
                }
            }
        }
        QDir::setCurrent(pwd);
    } else {
        cout << "Processing of project files have been disabled." << endl;
        cout << "Only use this option if you really know what you're doing." << endl << endl;
        return;
    }
}

void Configure::showSummary()
{
    QString make = dictionary[ "MAKE" ];
    cout << endl << endl << "Qt is now configured for building. Just run " << qPrintable(make) << "." << endl;
    cout << "To reconfigure, run " << qPrintable(make) << " confclean and configure." << endl << endl;
}

Configure::ProjectType Configure::projectType(const QString& proFileName)
{
    QFile proFile(proFileName);
    if (proFile.open(QFile::ReadOnly)) {
        QString buffer = proFile.readLine(1024);
        while (!buffer.isEmpty()) {
            QStringList segments = buffer.split(QRegExp("\\s"));
            QStringList::Iterator it = segments.begin();

            if (segments.size() >= 3) {
                QString keyword = (*it++);
                QString operation = (*it++);
                QString value = (*it++);

                if (keyword == "TEMPLATE") {
                    if (value == "lib")
                        return Lib;
                    else if (value == "subdirs")
                        return Subdirs;
                }
            }
            // read next line
            buffer = proFile.readLine(1024);
        }
        proFile.close();
    }
    // Default to app handling
    return App;
}

#if !defined(EVAL)

bool Configure::showLicense(QString orgLicenseFile)
{
    if (dictionary["LICENSE_CONFIRMED"] == "yes") {
        cout << "You have already accepted the terms of the license." << endl << endl;
        return true;
    }

    bool haveGpl3 = false;
    QString licenseFile = orgLicenseFile;
    QString theLicense;
    if (dictionary["EDITION"] == "OpenSource" || dictionary["EDITION"] == "Snapshot") {
        haveGpl3 = QFile::exists(orgLicenseFile + "/LICENSE.GPL3");
        theLicense = "GNU Lesser General Public License (LGPL) version 2.1";
        if (haveGpl3)
            theLicense += "\nor the GNU General Public License (GPL) version 3";
    } else {
        // the first line of the license file tells us which license it is
        QFile file(licenseFile);
        if (!file.open(QFile::ReadOnly)) {
            cout << "Failed to load LICENSE file" << endl;
            return false;
        }
        theLicense = file.readLine().trimmed();
    }

    forever {
        char accept = '?';
        cout << "You are licensed to use this software under the terms of" << endl
             << "the " << theLicense << "." << endl
             << endl;
        if (dictionary["EDITION"] == "OpenSource" || dictionary["EDITION"] == "Snapshot") {
            if (haveGpl3)
                cout << "Type '3' to view the GNU General Public License version 3 (GPLv3)." << endl;
            cout << "Type 'L' to view the Lesser GNU General Public License version 2.1 (LGPLv2.1)." << endl;
        } else {
            cout << "Type '?' to view the " << theLicense << "." << endl;
        }
        cout << "Type 'y' to accept this license offer." << endl
             << "Type 'n' to decline this license offer." << endl
             << endl
             << "Do you accept the terms of the license?" << endl;
        cin >> accept;
        accept = tolower(accept);

        if (accept == 'y') {
            return true;
        } else if (accept == 'n') {
            return false;
        } else {
            if (dictionary["EDITION"] == "OpenSource" || dictionary["EDITION"] == "Snapshot") {
                if (accept == '3')
                    licenseFile = orgLicenseFile + "/LICENSE.GPL3";
                else
                    licenseFile = orgLicenseFile + "/LICENSE.LGPL";
            }
            // Get console line height, to fill the screen properly
            int i = 0, screenHeight = 25; // default
            CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
            HANDLE stdOut = GetStdHandle(STD_OUTPUT_HANDLE);
            if (GetConsoleScreenBufferInfo(stdOut, &consoleInfo))
                screenHeight = consoleInfo.srWindow.Bottom
                             - consoleInfo.srWindow.Top
                             - 1; // Some overlap for context

            // Prompt the license content to the user
            QFile file(licenseFile);
            if (!file.open(QFile::ReadOnly)) {
                cout << "Failed to load LICENSE file" << licenseFile << endl;
                return false;
            }
            QStringList licenseContent = QString(file.readAll()).split('\n');
            while (i < licenseContent.size()) {
                cout << licenseContent.at(i) << endl;
                if (++i % screenHeight == 0) {
                    cout << "(Press any key for more..)";
                    if (_getch() == 3) // _Any_ keypress w/no echo(eat <Enter> for stdout)
                        exit(0);      // Exit cleanly for Ctrl+C
                    cout << "\r";     // Overwrite text above
                }
            }
        }
    }
}

void Configure::readLicense()
{
    dictionary["PLATFORM NAME"] = platformName();
    dictionary["LICENSE FILE"] = sourcePath;

    bool openSource = false;
    bool hasOpenSource = QFile::exists(dictionary["LICENSE FILE"] + "/LICENSE.GPL3") || QFile::exists(dictionary["LICENSE FILE"] + "/LICENSE.LGPL");
    if (dictionary["BUILDTYPE"] == "commercial") {
        openSource = false;
    } else if (dictionary["BUILDTYPE"] == "opensource") {
        openSource = true;
    } else if (hasOpenSource) { // No Open Source? Just display the commercial license right away
        forever {
            char accept = '?';
            cout << "Which edition of Qt do you want to use ?" << endl;
            cout << "Type 'c' if you want to use the Commercial Edition." << endl;
            cout << "Type 'o' if you want to use the Open Source Edition." << endl;
            cin >> accept;
            accept = tolower(accept);

            if (accept == 'c') {
                openSource = false;
                break;
            } else if (accept == 'o') {
                openSource = true;
                break;
            }
        }
    }
    if (hasOpenSource && openSource) {
        cout << endl << "This is the " << dictionary["PLATFORM NAME"] << " Open Source Edition." << endl;
        licenseInfo["LICENSEE"] = "Open Source";
        dictionary["EDITION"] = "OpenSource";
        dictionary["QT_EDITION"] = "QT_EDITION_OPENSOURCE";
        cout << endl;
        if (!showLicense(dictionary["LICENSE FILE"])) {
            cout << "Configuration aborted since license was not accepted";
            dictionary["DONE"] = "error";
            return;
        }
    } else if (openSource) {
        cout << endl << "Cannot find the GPL license files! Please download the Open Source version of the library." << endl;
        dictionary["DONE"] = "error";
    }
#ifdef COMMERCIAL_VERSION
    else {
        Tools::checkLicense(dictionary, licenseInfo, firstLicensePath());
        if (dictionary["DONE"] != "error") {
            // give the user some feedback, and prompt for license acceptance
            cout << endl << "This is the " << dictionary["PLATFORM NAME"] << " " << dictionary["EDITION"] << " Edition."<< endl << endl;
            if (!showLicense(dictionary["LICENSE FILE"])) {
                cout << "Configuration aborted since license was not accepted";
                dictionary["DONE"] = "error";
                return;
            }
        }
    }
#else // !COMMERCIAL_VERSION
    else {
        cout << endl << "Cannot build commercial edition from the open source version of the library." << endl;
        dictionary["DONE"] = "error";
    }
#endif
}

void Configure::reloadCmdLine()
{
    if (dictionary[ "REDO" ] == "yes") {
        QFile inFile(buildPath + "/configure" + dictionary[ "CUSTOMCONFIG" ] + ".cache");
        if (inFile.open(QFile::ReadOnly)) {
            QTextStream inStream(&inFile);
            QString buffer;
            inStream >> buffer;
            while (buffer.length()) {
                configCmdLine += buffer;
                inStream >> buffer;
            }
            inFile.close();
        }
    }
}

void Configure::saveCmdLine()
{
    if (dictionary[ "REDO" ] != "yes") {
        QFile outFile(buildPath + "/configure" + dictionary[ "CUSTOMCONFIG" ] + ".cache");
        if (outFile.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream outStream(&outFile);
            for (QStringList::Iterator it = configCmdLine.begin(); it != configCmdLine.end(); ++it) {
                outStream << (*it) << " " << endl;
            }
            outStream.flush();
            outFile.close();
        }
    }
}
#endif // !EVAL

bool Configure::isDone()
{
    return !dictionary["DONE"].isEmpty();
}

bool Configure::isOk()
{
    return (dictionary[ "DONE" ] != "error");
}

QString Configure::platformName() const
{
    switch (platform()) {
    default:
    case WINDOWS:
        return QStringLiteral("Qt for Windows");
    case WINDOWS_CE:
        return QStringLiteral("Qt for Windows CE");
    case QNX:
        return QStringLiteral("Qt for QNX");
    case BLACKBERRY:
        return QStringLiteral("Qt for Blackberry");
    }
}

QString Configure::qpaPlatformName() const
{
    switch (platform()) {
    default:
    case WINDOWS:
    case WINDOWS_CE:
        return QStringLiteral("windows");
    case QNX:
        return QStringLiteral("qnx");
    case BLACKBERRY:
        return QStringLiteral("blackberry");
    }
}

int Configure::platform() const
{
    const QString qMakeSpec = dictionary.value("QMAKESPEC");
    const QString xQMakeSpec = dictionary.value("XQMAKESPEC");

    if ((qMakeSpec.startsWith("wince") || xQMakeSpec.startsWith("wince")))
        return WINDOWS_CE;

    if (xQMakeSpec.contains("qnx"))
        return QNX;

    if (xQMakeSpec.contains("blackberry"))
        return BLACKBERRY;

    return WINDOWS;
}

bool
Configure::filesDiffer(const QString &fn1, const QString &fn2)
{
    QFile file1(fn1), file2(fn2);
    if (!file1.open(QFile::ReadOnly) || !file2.open(QFile::ReadOnly))
        return true;
    const int chunk = 2048;
    int used1 = 0, used2 = 0;
    char b1[chunk], b2[chunk];
    while (!file1.atEnd() && !file2.atEnd()) {
        if (!used1)
            used1 = file1.read(b1, chunk);
        if (!used2)
            used2 = file2.read(b2, chunk);
        if (used1 > 0 && used2 > 0) {
            const int cmp = qMin(used1, used2);
            if (memcmp(b1, b2, cmp))
                return true;
            if ((used1 -= cmp))
                memcpy(b1, b1+cmp, used1);
            if ((used2 -= cmp))
                memcpy(b2, b2+cmp, used2);
        }
    }
    return !file1.atEnd() || !file2.atEnd();
}

QT_END_NAMESPACE
