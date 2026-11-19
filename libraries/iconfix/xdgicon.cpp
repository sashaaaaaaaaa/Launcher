/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * Razor - a lightweight, Qt based, desktop toolset
 * http://razor-qt.org
 *
 * Copyright: 2010-2011 Razor team
 * Authors:
 *   Alexander Sokoloff <sokoloff.a@gmail.com>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */

#include "xdgicon.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QStringList>
#include <QTextStream>

namespace
{

QString fallbackThemeName;

void ensureResourceThemeSearchPath()
{
    QStringList paths = QIcon::themeSearchPaths();
    const QString resourcePath(QLatin1String(":/icons"));
    if (!paths.contains(resourcePath))
    {
        paths.prepend(resourcePath);
        QIcon::setThemeSearchPaths(paths);
    }
}

QString themeIconName(const QString &iconName)
{
    QString name = QFileInfo(iconName).fileName();
    static const QStringList extensions { QLatin1String(".png"), QLatin1String(".svg"), QLatin1String(".xpm") };
    for (const QString &extension : extensions)
    {
        if (name.endsWith(extension, Qt::CaseInsensitive))
        {
            name.truncate(name.length() - extension.length());
            break;
        }
    }
    return name;
}

QIcon iconFromFallbackTheme(const QString &name)
{
    if (fallbackThemeName.isEmpty())
        return QIcon();

    ensureResourceThemeSearchPath();

    const QString themeRoot = QLatin1String(":/icons/") + fallbackThemeName;
    QFile indexFile(themeRoot + QLatin1String("/index.theme"));
    if (!indexFile.open(QIODevice::ReadOnly))
        return QIcon();

    QStringList directories;
    bool inIconThemeSection = false;
    QTextStream stream(&indexFile);
    while (!stream.atEnd())
    {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1Char('[')))
        {
            inIconThemeSection = line.compare(QLatin1String("[Icon Theme]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inIconThemeSection)
            continue;
        if (line.startsWith(QLatin1String("Directories="), Qt::CaseInsensitive))
        {
            directories = line.mid(QLatin1String("Directories=").length()).split(QLatin1Char(','), Qt::SkipEmptyParts);
            break;
        }
    }

    static const QStringList extensions { QLatin1String(".svg"), QLatin1String(".png"), QLatin1String(".xpm") };
    for (const QString &directory : directories)
    {
        for (const QString &extension : extensions)
        {
            const QString path = themeRoot + QLatin1Char('/') + directory + QLatin1Char('/') + name + extension;
            if (QFileInfo::exists(path))
                return QIcon(path);
        }
    }

    return QIcon();
}

} // namespace

/************************************************

 ************************************************/
XdgIcon::XdgIcon()
{
}

/************************************************

 ************************************************/
XdgIcon::~XdgIcon()
{
}

/************************************************
 Returns the name of the current icon theme.
 ************************************************/
QString XdgIcon::themeName()
{
    return QIcon::themeName();
}

/************************************************
 Sets the current icon theme to name.
 ************************************************/
void XdgIcon::setThemeName(const QString &themeName)
{
    ensureResourceThemeSearchPath();
    QIcon::setThemeName(themeName);
}

void XdgIcon::setFallbackThemeName(const QString &themeName)
{
    fallbackThemeName = themeName;
    ensureResourceThemeSearchPath();
}

/************************************************
 Returns the QIcon corresponding to name in the current icon theme. If no such icon
 is found in the current theme fallback is returned.
 ************************************************/
QIcon XdgIcon::fromTheme(const QString &iconName, const QIcon &fallback)
{
    if (iconName.isEmpty())
        return fallback;

    ensureResourceThemeSearchPath();

    if (QFileInfo(iconName).isAbsolute())
        return QIcon(iconName);

    const QString name = themeIconName(iconName);
    if (name.isEmpty())
        return fallback;

    QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull())
        return icon;

    icon = iconFromFallbackTheme(name);
    if (!icon.isNull())
        return icon;

    return fallback;
}

/************************************************
 Returns the QIcon corresponding to names in the current icon theme. If no such icon
 is found in the current theme fallback is returned.
 ************************************************/
QIcon XdgIcon::fromTheme(const QStringList &iconNames, const QIcon &fallback)
{
    for (const QString &iconName : iconNames)
    {
        QIcon icon = fromTheme(iconName);
        if (!icon.isNull())
            return icon;
    }

    return fallback;
}
