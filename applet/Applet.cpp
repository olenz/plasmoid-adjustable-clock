/***********************************************************************************
* Adjustable Clock: Plasmoid to show date and time in adjustable format.
* Copyright (C) 2008 - 2013 Michal Dutkiewicz aka Emdek <emdeck@gmail.com>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*
***********************************************************************************/

#include "Applet.h"
#include "Clock.h"
#include "Configuration.h"

#include <QtCore/QFile>
#include <QtCore/QXmlStreamReader>
#include <QtCore/QXmlStreamWriter>
#include <QtCore/QFileSystemWatcher>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtWebKit/QWebPage>
#include <QtWebKit/QWebFrame>

#include <KMenu>
#include <KLocale>
#include <KConfigDialog>
#include <KStandardDirs>

#include <Plasma/Theme>
#include <Plasma/Containment>

K_EXPORT_PLASMA_APPLET(adjustableclock, AdjustableClock::Applet)

namespace AdjustableClock
{

Applet::Applet(QObject *parent, const QVariantList &args) : ClockApplet(parent, args),
    m_clock(new Clock(this)),
    m_clipboardAction(NULL),
    m_theme(-1)
{
    KGlobal::locale()->insertCatalog(QLatin1String("libplasmaclock"));
    KGlobal::locale()->insertCatalog(QLatin1String("timezones4"));
    KGlobal::locale()->insertCatalog(QLatin1String("adjustableclock"));

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    setHasConfigurationInterface(true);
    resize(120, 80);
}

void Applet::init()
{
    ClockApplet::init();

    QPalette palette = m_page.palette();
    palette.setBrush(QPalette::Base, Qt::transparent);

    m_page.setPalette(palette);
    m_page.mainFrame()->setScrollBarPolicy(Qt::Horizontal, Qt::ScrollBarAlwaysOff);
    m_page.mainFrame()->setScrollBarPolicy(Qt::Vertical, Qt::ScrollBarAlwaysOff);

    constraintsEvent(Plasma::SizeConstraint);
    configChanged();

    QFileSystemWatcher *watcher = new QFileSystemWatcher(this);
    watcher->addPath(KStandardDirs::locate("data", QLatin1String("adjustableclock/themes.xml")));
    watcher->addPath(KStandardDirs::locateLocal("data", QLatin1String("adjustableclock/custom-themes.xml")));

    connect(this, SIGNAL(activate()), this, SLOT(copyToClipboard()));
    connect(&m_page, SIGNAL(repaintRequested(QRect)), this, SLOT(repaint()));
    connect(watcher, SIGNAL(fileChanged(QString)), this, SLOT(clockConfigChanged()));
    connect(Plasma::Theme::defaultTheme(), SIGNAL(themeChanged()), this, SLOT(updateTheme()));
}

void Applet::constraintsEvent(Plasma::Constraints constraints)
{
    Q_UNUSED(constraints)

    setBackgroundHints(getTheme().background ? DefaultBackground : NoBackground);
}

void Applet::resizeEvent(QGraphicsSceneResizeEvent *event)
{
    ClockApplet::resizeEvent(event);

    updateSize();
}

void Applet::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->buttons() == Qt::MidButton) {
        copyToClipboard();
    }

    const QUrl url = m_page.mainFrame()->hitTestContent(event->pos().toPoint()).linkUrl();

    if (url.isValid() && event->button() == Qt::LeftButton) {
        QDesktopServices::openUrl(url);

        event->ignore();
    } else {
        ClockApplet::mousePressEvent(event);
    }
}

void Applet::paintInterface(QPainter *painter, const QStyleOptionGraphicsItem *option, const QRect &contentsRect)
{
    Q_UNUSED(option)
    Q_UNUSED(contentsRect)

    painter->setRenderHint(QPainter::SmoothPixmapTransform);

    m_page.mainFrame()->render(painter);
}

void Applet::createClockConfigurationInterface(KConfigDialog *parent)
{
    Configuration *configuration = new Configuration(this, parent);

    connect(configuration, SIGNAL(accepted()), this, SIGNAL(configNeedsSaving()));
    connect(configuration, SIGNAL(accepted()), this, SLOT(configChanged()));
}

void Applet::clockConfigChanged()
{
    const QString path = KStandardDirs::locate("data", QLatin1String("adjustableclock/themes.xml"));
    const QStringList customThemes = config().group("Formats").groupList();

    m_themes = loadThemes(path, true);

    QList<Theme> themes = loadThemes(KStandardDirs::locateLocal("data", QLatin1String("adjustableclock/custom-themes.xml")), false);
    const int amount = themes.count();

    if (!customThemes.isEmpty()) {
        for (int i = 0; i < customThemes.count(); ++i) {
            KConfigGroup themeConfiguration = config().group("Formats").group(customThemes.at(i));

            if (themeConfiguration.readEntry("html", QString()).isEmpty()) {
                continue;
            }

            Theme theme;
            theme.id = themeConfiguration.readEntry("title", i18n("Custom"));
            theme.title = themeConfiguration.readEntry("title", i18n("Custom"));
            theme.html = themeConfiguration.readEntry("html", QString());
            theme.css = themeConfiguration.readEntry("css", QString());
            theme.script = themeConfiguration.readEntry("script", QString());
            theme.background = themeConfiguration.readEntry("background", true);
            theme.bundled = false;

            themes.append(theme);
        }

        config().deleteGroup(QLatin1String("Formats"));

        if (themes.count() != amount) {
            saveCustomThemes(themes);
        }
    }

    m_themes.append(themes);

    if (m_themes.isEmpty()) {
        Theme theme;
        theme.id = QLatin1String("%default%");
        theme.title = i18n("Error");
        theme.html = i18n("Missing or invalid data file: %1.").arg(path);
        theme.background = true;
        theme.bundled = false;

        m_theme = 0;

        m_themes.append(theme);
    } else {
        const QString id = config().readEntry("format", "%default%");

        m_theme = -1;

        for (int i = 0; i < m_themes.count(); ++i) {
            if (m_themes.at(i).id == id) {
                m_theme = i;

                break;
            }
        }

        if (m_theme < 0) {
            m_theme = 0;
        }
    }

    changeEngineTimezone(currentTimezone(), currentTimezone());
}

void Applet::clockConfigAccepted()
{
    emit configNeedsSaving();
}

void Applet::changeEngineTimezone(const QString &oldTimezone, const QString &newTimezone)
{
    dataEngine(QLatin1String("time"))->disconnectSource(oldTimezone, m_clock);

    m_clock->connectSource(newTimezone);

    constraintsEvent(Plasma::SizeConstraint);
    updateSize();
}

void Applet::copyToClipboard()
{
    QApplication::clipboard()->setText(m_clock->evaluateFormat(config().readEntry("fastCopyFormat", "%Y-%M-%d %h:%m:%s"), m_clock->getCurrentDateTime()));
}

void Applet::copyToClipboard(QAction *action)
{
    QApplication::clipboard()->setText(action->text());
}

void Applet::toolTipAboutToShow()
{
    updateToolTipContent();
}

void Applet::toolTipHidden()
{
    Plasma::ToolTipManager::self()->clearContent(this);
}

void Applet::repaint()
{
    update();
}

void Applet::saveCustomThemes(const QList<Theme> &themes)
{
    QFile file(KStandardDirs::locateLocal("data", QLatin1String("adjustableclock/custom-themes.xml")));
    file.open(QFile::WriteOnly | QFile::Text);

    QXmlStreamWriter stream(&file);
    stream.setAutoFormatting(true);
    stream.writeStartDocument();
    stream.writeStartElement(QLatin1String("themes"));

    for (int i = 0; i < themes.count(); ++i) {
        stream.writeStartElement(QLatin1String("theme"));
        stream.writeStartElement(QLatin1String("id"));
        stream.writeCharacters(themes.at(i).id);
        stream.writeEndElement();
        stream.writeStartElement(QLatin1String("title"));
        stream.writeCharacters(themes.at(i).title);
        stream.writeEndElement();
        stream.writeStartElement(QLatin1String("background"));
        stream.writeCharacters(themes.at(i).background?QLatin1String("true"):QLatin1String("false"));
        stream.writeEndElement();
        stream.writeStartElement(QLatin1String("html"));
        stream.writeCDATA(themes.at(i).html);
        stream.writeEndElement();
        stream.writeStartElement(QLatin1String("css"));
        stream.writeCDATA(themes.at(i).css);
        stream.writeEndElement();
        stream.writeStartElement(QLatin1String("script"));
        stream.writeCDATA(themes.at(i).script);
        stream.writeEndElement();
        stream.writeEndElement();
    }

    stream.writeEndElement();
    stream.writeEndDocument();

    file.close();
}

void Applet::updateClipboardMenu()
{
    const QDateTime dateTime = m_clock->getCurrentDateTime();
    const QStringList clipboardFormats = getClipboardFormats();

    qDeleteAll(m_clipboardAction->menu()->actions());

    m_clipboardAction->menu()->clear();

    for (int i = 0; i < clipboardFormats.count(); ++i) {
        if (clipboardFormats.at(i).isEmpty()) {
            m_clipboardAction->menu()->addSeparator();
        } else {
            m_clipboardAction->menu()->addAction(m_clock->evaluateFormat(clipboardFormats.at(i), dateTime));
        }
    }
}

void Applet::updateToolTipContent()
{
    Plasma::ToolTipContent toolTipData;
    QPair<QString, QString> toolTipFormat = getToolTipFormat();

    if (!toolTipFormat.first.isEmpty() || !toolTipFormat.second.isEmpty()) {
        const QDateTime dateTime = m_clock->getCurrentDateTime();

        toolTipData.setImage(KIcon(QLatin1String("chronometer")).pixmap(IconSize(KIconLoader::Desktop)));
        toolTipData.setMainText(m_clock->evaluateFormat(toolTipFormat.first, dateTime));
        toolTipData.setSubText(m_clock->evaluateFormat(toolTipFormat.second, dateTime));
        toolTipData.setAutohide(false);
    }

    Plasma::ToolTipManager::self()->setContent(this, toolTipData);
}

void Applet::updateSize()
{
    const Theme theme = getTheme();

    setTheme(m_clock->evaluateFormat(theme.html), theme.css, theme.script);

    m_page.setViewportSize(QSize(0, 0));
    m_page.mainFrame()->setZoomFactor(1);

    QSizeF size;

    if (formFactor() == Plasma::Horizontal) {
        size = QSizeF(containment()->boundingRect().width(), boundingRect().height());
    } else if (formFactor() == Plasma::Vertical) {
        size = QSizeF(boundingRect().width(), containment()->boundingRect().height());
    } else {
        if (theme.background) {
            size = contentsRect().size();
        } else {
            size = boundingRect().size();
        }
    }

    const qreal widthFactor = (size.width() / m_page.mainFrame()->contentsSize().width());
    const qreal heightFactor = (size.height() / m_page.mainFrame()->contentsSize().height());

    m_page.mainFrame()->setZoomFactor((widthFactor > heightFactor) ? heightFactor : widthFactor);

    if (formFactor() == Plasma::Horizontal) {
        setMinimumWidth(m_page.mainFrame()->contentsSize().width());
        setMinimumHeight(0);
    } else if (formFactor() == Plasma::Vertical) {
        setMinimumHeight(m_page.mainFrame()->contentsSize().height());
        setMinimumWidth(0);
    }

    m_page.setViewportSize(boundingRect().size().toSize());

    setTheme(m_clock->evaluateFormat(theme.html, m_clock->getCurrentDateTime(false)), theme.css, theme.script);
}

void Applet::updateTheme()
{
    const QString html = m_currentHtml;
    const Theme theme = getTheme();

    m_page.settings()->setFontFamily(QWebSettings::StandardFont, Plasma::Theme::defaultTheme()->font(Plasma::Theme::DefaultFont).family());

    m_currentHtml = QString();

    setTheme(html, theme.css, theme.script);
}

void Applet::setTheme(const QString &html, const QString &css, const QString &script)
{
    if (html != m_currentHtml) {
        m_currentHtml = html;

        m_page.mainFrame()->setHtml(getPageLayout(html, css, script));
    }
}

Clock* Applet::getClock() const
{
    return m_clock;
}

QString Applet::getPageLayout(const QString &html, const QString &css, const QString &script, const QString &head)
{
    return (QLatin1String("<!DOCTYPE html><html><head><style type=\"text/css\">* {color: ") + Plasma::Theme::defaultTheme()->color(Plasma::Theme::TextColor).name() + QLatin1String(";} html, body, body > div {margin: 0; padding: 0; height: 100%; width: 100%; vertical-align: middle;} body {display: table;} body > div {display: table-cell;}") + css + QLatin1String("</style><script type=\"text/javascript\">") + script + QLatin1String("</script>") + head + QLatin1String("</head><body><div>") + html + QLatin1String("</div></body></html>"));
}

Theme Applet::getTheme() const
{
    if (m_theme >= 0 && m_theme < m_themes.count()) {
        return m_themes[m_theme];
    }

    Theme theme;
    theme.id = QLatin1String("%default%");
    theme.title = i18n("Error");
    theme.html = i18n("Invalid theme identifier.");
    theme.background = true;
    theme.bundled = false;

    return theme;
}

QPair<QString, QString> Applet::getToolTipFormat() const
{
    QPair<QString, QString> toolTipFormat;
    toolTipFormat.first = (config().keyList().contains(QLatin1String("toolTipFormatMain")) ? config().readEntry("toolTipFormatMain", QString()) : QLatin1String("<div style=\"text-align:center;\">%h:%m:%s<br>%$w, %d.%M.%Y</div>"));
    toolTipFormat.second = (config().keyList().contains(QLatin1String("toolTipFormatSub")) ? config().readEntry("toolTipFormatSub", QString()) : QLatin1String("%!Z%E"));

    return toolTipFormat;
}

QStringList Applet::getClipboardFormats() const
{
    QStringList clipboardFormats;
    clipboardFormats << QLatin1String("%!t")
    << QLatin1String("%t")
    << QLatin1String("%h:%m:%s")
    << QString()
    << QLatin1String("%!T")
    << QLatin1String("%T")
    << QString()
    << QLatin1String("%!A")
    << QLatin1String("%A")
    << QLatin1String("%Y-%M-%d %h:%m:%s")
    << QString()
    << QLatin1String("%U");

    return config().readEntry("clipboardFormats", clipboardFormats);
}

QList<Theme> Applet::getThemes() const
{
    return m_themes;
}

QList<Theme> Applet::loadThemes(const QString &path, bool bundled) const
{
    QList<Theme> themes;
    QFile file(path);
    file.open(QFile::ReadOnly | QFile::Text);

    QXmlStreamReader reader(&file);
    Theme theme;
    theme.bundled = bundled;

    while (!reader.atEnd()) {
        reader.readNext();

        if (!reader.isStartElement()) {
            if (reader.name().toString() == QLatin1String("theme")) {
                themes.append(theme);
            }

            continue;
        }

        if (reader.name().toString() == QLatin1String("theme")) {
            theme.id = QString();
            theme.title = QString();
            theme.description = QString();
            theme.author = QString();
            theme.html = QString();
            theme.css = QString();
            theme.script = QString();
            theme.background = true;
        }

        if (reader.name().toString() == QLatin1String("id")) {
            if (bundled) {
                theme.id = QLatin1Char('%') + reader.readElementText() + QLatin1Char('%');
            } else {
                theme.id = reader.readElementText();
           }
        }

        if (reader.name().toString() == QLatin1String("title")) {
            theme.title = i18n(reader.readElementText().toUtf8().data());
        }

        if (reader.name().toString() == QLatin1String("description")) {
            theme.description = i18n(reader.readElementText().toUtf8().data());
        }

        if (reader.name().toString() == QLatin1String("author")) {
            theme.author = reader.readElementText();
        }

        if (reader.name().toString() == QLatin1String("background")) {
            theme.background = (reader.readElementText().toLower() == QLatin1String("true"));
        }

        if (reader.name().toString() == QLatin1String("html")) {
            theme.html = reader.readElementText();
        }

        if (reader.name().toString() == QLatin1String("css")) {
            theme.css = reader.readElementText();
        }

        if (reader.name().toString() == QLatin1String("script")) {
            theme.script = reader.readElementText();
        }
    }

    file.close();

    return themes;
}

QList<QAction*> Applet::contextualActions()
{
    QList<QAction*> actions = ClockApplet::contextualActions();

    if (!m_clipboardAction) {
        m_clipboardAction = new QAction(SmallIcon(QLatin1String("edit-copy")), i18n("C&opy to Clipboard"), this);
        m_clipboardAction->setMenu(new KMenu);

        connect(this, SIGNAL(destroyed()), m_clipboardAction->menu(), SLOT(deleteLater()));
        connect(m_clipboardAction->menu(), SIGNAL(aboutToShow()), this, SLOT(updateClipboardMenu()));
        connect(m_clipboardAction->menu(), SIGNAL(triggered(QAction*)), this, SLOT(copyToClipboard(QAction*)));
    }

    for (int i = 0; i < actions.count(); ++i) {
        if (actions.at(i)->text() == i18n("C&opy to Clipboard")) {
            actions.removeAt(i);
            actions.insert(i, m_clipboardAction);

            m_clipboardAction->setVisible(!getClipboardFormats().isEmpty());
        }
    }

    return actions;
}

}
