/***************************************************************************
 *   Copyright (C) 2008 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA          *
 ***************************************************************************/


#include "collapsibleeffect.h"

#include "ui_listval_ui.h"
#include "ui_boolval_ui.h"
#include "ui_wipeval_ui.h"
#include "ui_urlval_ui.h"
#include "ui_keywordval_ui.h"
#include "ui_fontval_ui.h"
#include "complexparameter.h"
#include "geometryval.h"
#include "positionedit.h"
#include "projectlist.h"
#include "effectslist.h"
#include "kdenlivesettings.h"
#include "profilesdialog.h"
#include "kis_curve_widget.h"
#include "kis_cubic_curve.h"
#include "choosecolorwidget.h"
#include "geometrywidget.h"
#include "colortools.h"
#include "doubleparameterwidget.h"
#include "cornerswidget.h"
#include "dragvalue.h"
#include "beziercurve/beziersplinewidget.h"
#ifdef USE_QJSON
#include "rotoscoping/rotowidget.h"
#endif

#include <QInputDialog>
#include <QDialog>
#include <QMenu>
#include <QVBoxLayout>

#include <KDebug>
#include <KGlobalSettings>
#include <KLocale>
#include <KMessageBox>
#include <KStandardDirs>
#include <KFileDialog>
#include <KUrlRequester>
#include <KColorScheme>

class Boolval: public QWidget, public Ui::Boolval_UI
{
};

class Listval: public QWidget, public Ui::Listval_UI
{
};

class Wipeval: public QWidget, public Ui::Wipeval_UI
{
};

class Urlval: public QWidget, public Ui::Urlval_UI
{
};

class Keywordval: public QWidget, public Ui::Keywordval_UI
{
};

class Fontval: public QWidget, public Ui::Fontval_UI
{
};

QMap<QString, QImage> CollapsibleEffect::iconCache;

void clearLayout(QLayout *layout)
{
    QLayoutItem *item;
    while((item = layout->takeAt(0))) {
        if (item->layout()) {
            clearLayout(item->layout());
            delete item->layout();
        }
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }
}

MySpinBox::MySpinBox(QWidget * parent):
    QSpinBox(parent)
{
    setFocusPolicy(Qt::StrongFocus);
}

void MySpinBox::focusInEvent(QFocusEvent*)
{
     setFocusPolicy(Qt::WheelFocus);
}

void MySpinBox::focusOutEvent(QFocusEvent*)
{
     setFocusPolicy(Qt::StrongFocus);
}


CollapsibleEffect::CollapsibleEffect(QDomElement effect, QDomElement original_effect, ItemInfo info, int ix, EffectMetaInfo *metaInfo, bool lastEffect, bool isGroup, QWidget * parent) :
        QWidget(parent),
        m_paramWidget(NULL),
        m_effect(effect),
        m_original_effect(original_effect),
        m_lastEffect(lastEffect),
        m_isGroup(isGroup),
        m_index(ix)
{
    setupUi(this);
    filterWheelEvent = true;
    m_info.fromString(effect.attribute("kdenlive_info"));
    setFont(KGlobalSettings::smallestReadableFont());
   
    buttonUp->setIcon(KIcon("kdenlive-up"));
    buttonUp->setToolTip(i18n("Move effect up"));
    if (!lastEffect) {
        buttonDown->setIcon(KIcon("kdenlive-down"));
        buttonDown->setToolTip(i18n("Move effect down"));
    }
    buttonDel->setIcon(KIcon("kdenlive-deleffect"));
    buttonDel->setToolTip(i18n("Delete effect"));
    if (effectIndex() == 1) buttonUp->setVisible(false);
    if (m_lastEffect) buttonDown->setVisible(false);
    //buttonUp->setVisible(false);
    //buttonDown->setVisible(false);
    
    /*buttonReset->setIcon(KIcon("view-refresh"));
    buttonReset->setToolTip(i18n("Reset effect"));*/
    //checkAll->setToolTip(i18n("Enable/Disable all effects"));
    //buttonShowComments->setIcon(KIcon("help-about"));
    //buttonShowComments->setToolTip(i18n("Show additional information for the parameters"));
    m_menu = new QMenu;
    m_menu->addAction(KIcon("view-refresh"), i18n("Reset effect"), this, SLOT(slotResetEffect()));
    m_menu->addAction(KIcon("document-save"), i18n("Save effect"), this, SLOT(slotSaveEffect()));
    
    if (!m_isGroup) {
	QDomElement namenode = m_effect.firstChildElement("name");
	if (namenode.isNull()) return;
	title->setText(i18n(namenode.text().toUtf8().data()));
	QString type = m_effect.attribute("type", QString());
	KIcon icon;
	if (type == "audio") icon = KIcon("kdenlive-show-audio");
	else if (m_effect.attribute("tag") == "region") icon = KIcon("kdenlive-mask-effect");
	else if (type == "custom") icon = KIcon("kdenlive-custom-effect");
	else icon = KIcon("kdenlive-show-video");
	effecticon->setPixmap(icon.pixmap(16,16));
	m_menu->addAction(KIcon("folder-new"), i18n("Create Group"), this, SLOT(slotCreateGroup()));
	setupWidget(info, ix, metaInfo);
    }
    else {
	title->setText(i18n("Effect Group"));
	effecticon->setPixmap(KIcon("folder").pixmap(16,16));
	m_menu->addAction(KIcon("list-remove"), i18n("Ungroup"), this, SLOT(slotUnGroup()));
    }
    setAcceptDrops(true);
    menuButton->setIcon(KIcon("kdenlive-menu"));
    menuButton->setMenu(m_menu);
    
    if (m_effect.attribute("disable") == "1") {
        title->setEnabled(false);
	enabledBox->setChecked(false);
    }
    else {
        enabledBox->setChecked(true);
    }

    connect(collapseButton, SIGNAL(clicked()), this, SLOT(slotSwitch()));
    connect(enabledBox, SIGNAL(toggled(bool)), this, SLOT(slotEnable(bool)));
    connect(buttonUp, SIGNAL(clicked()), this, SLOT(slotEffectUp()));
    connect(buttonDown, SIGNAL(clicked()), this, SLOT(slotEffectDown()));
    connect(buttonDel, SIGNAL(clicked()), this, SLOT(slotDeleteEffect()));

    Q_FOREACH( QSpinBox * sp, findChildren<QSpinBox*>() ) {
        sp->installEventFilter( this );
        sp->setFocusPolicy( Qt::StrongFocus );
    }
    Q_FOREACH( KComboBox * cb, findChildren<KComboBox*>() ) {
	cb->installEventFilter( this );
        cb->setFocusPolicy( Qt::StrongFocus );
    }
    Q_FOREACH( QProgressBar * cb, findChildren<QProgressBar*>() ) {
	cb->installEventFilter( this );
        cb->setFocusPolicy( Qt::StrongFocus );
    }
}

CollapsibleEffect::~CollapsibleEffect()
{
    if (m_paramWidget) delete m_paramWidget;
    delete m_menu;
}

//static
const QString CollapsibleEffect::getStyleSheet(QPalette p)
{
    KColorScheme scheme(p.currentColorGroup(), KColorScheme::View, KSharedConfig::openConfig(KdenliveSettings::colortheme()));
    QColor dark_bg = scheme.shade(KColorScheme::DarkShade);
    QColor selected_bg = scheme.decoration(KColorScheme::FocusColor).color();
    QColor hover_bg = scheme.decoration(KColorScheme::HoverColor).color();
    QColor light_bg = scheme.shade(KColorScheme::LightShade);
    QColor normal_bg = scheme.background(KColorScheme::AlternateBackground).color();
    QColor alt_bg = scheme.background(KColorScheme::NormalBackground).color();
    
    KColorScheme scheme2(p.currentColorGroup(), KColorScheme::Window, KSharedConfig::openConfig(KdenliveSettings::colortheme()));
    QColor normal_bg2 = scheme2.background(KColorScheme::NormalBackground).color();

    QString stylesheet(QString("QFrame#decoframe {border-radius:5px;border:0px solid %1;border-top:0px;background:%6;} QFrame:hover#decoframe {background:%7;} QFrame#decoframe[active=\"true\"] {background:%5;} QFrame#frame[active=\"true\"] {background:%3;}  QProgressBar::chunk:horizontal {background: %6;border-top-left-radius: 4px;border-bottom-left-radius: 4px;} QProgressBar::chunk:horizontal#dragOnly {background: %5;border-top-left-radius: 4px;border-bottom-left-radius: 4px;} QProgressBar::chunk:horizontal:hover {background: %3;}\
    QProgressBar:horizontal {border: 1px solid %1;border-top-left-radius: 4px;border-bottom-left-radius: 4px;border-right:0px;background:%5;padding: 0px;text-align:left center}\
                                QProgressBar:horizontal:disabled {border: 1px solid %6} QProgressBar:horizontal#dragOnly {background: %5}\
                                QProgressBar:horizontal[inTimeline=\"true\"] { border: 1px solid %2;border-right: 0px;background: %4;padding: 0px;text-align:left center } QProgressBar::chunk:horizontal[inTimeline=\"true\"] {background: %2;}\
                                QAbstractSpinBox#dragBox {border: 1px solid %1;border-top-right-radius: 4px;border-bottom-right-radius: 4px;padding-right:0px;} QAbstractSpinBox::down-button#dragBox {width:0px;padding:0px;}\
                                QAbstractSpinBox:disabled#dragBox {border: 1px solid %6;}\
                                QAbstractSpinBox::up-button#dragBox {width:0px;padding:0px;} QAbstractSpinBox[inTimeline=\"true\"]#dragBox { border: 1px solid %2;} QAbstractSpinBox:hover#dragBox {border: 1px solid %3;} ")
                                .arg(dark_bg.name()).arg(hover_bg.name()).arg(selected_bg.name()).arg(light_bg.name()).arg(alt_bg.name()).arg(normal_bg2.name()).arg(normal_bg.name()));
    return stylesheet;/*
    QPalette p = QApplication::palette();
    KColorScheme scheme(p.currentColorGroup(), KColorScheme::View, KSharedConfig::openConfig(KdenliveSettings::colortheme()));
    QColor dark_bg = scheme.shade(KColorScheme::DarkShade);
    QColor selected_bg = scheme.decoration(KColorScheme::FocusColor).color();
    QColor hover_bg = scheme.decoration(KColorScheme::HoverColor).color();
    QColor light_bg = scheme.shade(KColorScheme::LightShade);
    QColor normal_bg = scheme.background(KColorScheme::NormalBackground).color();
    
    KColorScheme scheme2(p.currentColorGroup(), KColorScheme::Window, KSharedConfig::openConfig(KdenliveSettings::colortheme()));
    QColor normal_bg2 = scheme2.background(KColorScheme::NormalBackground).color();

    QString stylesheet(QString("QProgressBar::chunk:horizontal {background: %6;border-top-left-radius: 4px;border-bottom-left-radius: 4px;} QProgressBar::chunk:horizontal#dragOnly {background: %5;border-top-left-radius: 4px;border-bottom-left-radius: 4px;} QProgressBar::chunk:horizontal:hover {background: %3;}\
    QProgressBar:horizontal {border: 1px solid %1;border-top-left-radius: 4px;border-bottom-left-radius: 4px;border-right:0px;background:%5;padding: 0px;text-align:left center}\
                                QProgressBar:horizontal:disabled {border: 1px solid %6} QProgressBar:horizontal#dragOnly {background: %5}\
                                QProgressBar:horizontal[inTimeline=\"true\"] { border: 1px solid %2;border-right: 0px;background: %4;padding: 0px;text-align:left center } QProgressBar::chunk:horizontal[inTimeline=\"true\"] {background: %2;}\
                                QAbstractSpinBox#dragBox {border: 1px solid %1;border-top-right-radius: 4px;border-bottom-right-radius: 4px;padding-right:0px;} QAbstractSpinBox::down-button#dragBox {width:0px;padding:0px;}\
                                QAbstractSpinBox:disabled#dragBox {border: 1px solid %6;}\
                                QAbstractSpinBox::up-button#dragBox {width:0px;padding:0px;} QAbstractSpinBox[inTimeline=\"true\"]#dragBox { border: 1px solid %2;} QAbstractSpinBox:hover#dragBox {border: 1px solid %3;} ")
                                .arg(dark_bg.name()).arg(hover_bg.name()).arg(selected_bg.name()).arg(light_bg.name()).arg(normal_bg.name()).arg(normal_bg2.name()));
    return stylesheet;*/
}

void CollapsibleEffect::slotCreateGroup()
{
    emit createGroup(effectIndex());
}

void CollapsibleEffect::slotUnGroup()
{
    emit unGroup(this);
}

bool CollapsibleEffect::eventFilter( QObject * o, QEvent * e ) 
{
    if (e->type() == QEvent::Wheel) {
	QWheelEvent *we = static_cast<QWheelEvent *>(e);
	if (!filterWheelEvent || we->modifiers() != Qt::NoModifier) {
	    e->accept();
	    return false;
	}
	if (qobject_cast<QAbstractSpinBox*>(o)) {
	    if(qobject_cast<QAbstractSpinBox*>(o)->focusPolicy() == Qt::WheelFocus)
	    {
		e->accept();
		return false;
	    }
	    else
	    {
		e->ignore();
		return true;
	    }
	}
	if (qobject_cast<KComboBox*>(o)) {
	    if(qobject_cast<KComboBox*>(o)->focusPolicy() == Qt::WheelFocus)
	    {
		e->accept();
		return false;
	    }
	    else
	    {
		e->ignore();
		return true;
	    }
	}
	if (qobject_cast<QProgressBar*>(o)) {
	    if(qobject_cast<QProgressBar*>(o)->focusPolicy() == Qt::WheelFocus)
	    {
		e->accept();
		return false;
	    }
	    else
	    {
		e->ignore();
		return true;
	    }
	}
    }
    return QWidget::eventFilter(o, e);
}

QDomElement CollapsibleEffect::effect() const
{
    return m_effect;
}

bool CollapsibleEffect::isActive() const
{
    return decoframe->property("active").toBool();
}

void CollapsibleEffect::setActive(bool activate)
{
    decoframe->setProperty("active", activate);
    decoframe->setStyleSheet(decoframe->styleSheet());
}

void CollapsibleEffect::mouseDoubleClickEvent ( QMouseEvent * event )
{
    if (frame->underMouse() && collapseButton->isEnabled()) slotSwitch();
    QWidget::mouseDoubleClickEvent(event);
}

void CollapsibleEffect::mousePressEvent ( QMouseEvent *event )
{
  
    if (!decoframe->property("active").toBool() && !isGroup()) emit activateEffect(effectIndex());
    QWidget::mousePressEvent(event);
}


void CollapsibleEffect::slotEnable(bool enable)
{
    title->setEnabled(enable);
    enabledBox->blockSignals(true);
    enabledBox->setChecked(enable);
    enabledBox->blockSignals(false);
    if (m_isGroup) {
	QVBoxLayout *vbox = static_cast<QVBoxLayout *>(widgetFrame->layout());
	if (vbox == NULL) return;
	for (int i = 0; i < vbox->count(); i++) {
	    CollapsibleEffect *e = static_cast<CollapsibleEffect *>(vbox->itemAt(i)->widget());
	    if (e) e->enabledBox->setChecked(enable);// slotEnable(enable);
	}
    } else {
	m_effect.setAttribute("disable", enable ? 0 : 1);
	if (enable || KdenliveSettings::disable_effect_parameters()) {
	    widgetFrame->setEnabled(enable);
	}
	emit effectStateChanged(!enable, effectIndex());
    }
}

void CollapsibleEffect::slotDeleteEffect()
{
    if (!m_isGroup) emit deleteEffect(m_effect);
}

void CollapsibleEffect::slotEffectUp()
{
    if (!m_isGroup) emit changeEffectPosition(effectIndex(), true);
}

void CollapsibleEffect::slotEffectDown()
{
    if (!m_isGroup) emit changeEffectPosition(effectIndex(), false);
}

void CollapsibleEffect::slotSaveEffect()
{
    QString name = QInputDialog::getText(this, i18n("Save Effect"), i18n("Name for saved effect: "));
    if (name.isEmpty()) return;
    QString path = KStandardDirs::locateLocal("appdata", "effects/", true);
    path = path + name + ".xml";
    if (QFile::exists(path)) if (KMessageBox::questionYesNo(this, i18n("File %1 already exists.\nDo you want to overwrite it?", path)) == KMessageBox::No) return;

    QDomDocument doc;
    QDomElement effect = m_effect.cloneNode().toElement();
    doc.appendChild(doc.importNode(effect, true));
    effect = doc.firstChild().toElement();
    effect.removeAttribute("kdenlive_ix");
    effect.setAttribute("id", name);
    effect.setAttribute("type", "custom");
    QDomElement effectname = effect.firstChildElement("name");
    effect.removeChild(effectname);
    effectname = doc.createElement("name");
    QDomText nametext = doc.createTextNode(name);
    effectname.appendChild(nametext);
    effect.insertBefore(effectname, QDomNode());
    QDomElement effectprops = effect.firstChildElement("properties");
    effectprops.setAttribute("id", name);
    effectprops.setAttribute("type", "custom");

    QFile file(path);
    if (file.open(QFile::WriteOnly | QFile::Truncate)) {
        QTextStream out(&file);
        out << doc.toString();
    }
    file.close();
    emit reloadEffects();
}

void CollapsibleEffect::slotResetEffect()
{
    emit resetEffect(effectIndex());
}

void CollapsibleEffect::slotSwitch()
{
    bool enable = !widgetFrame->isVisible();
    slotShow(enable);
}

void CollapsibleEffect::slotShow(bool show)
{
    widgetFrame->setVisible(show);
    if (show) {
        collapseButton->setArrowType(Qt::DownArrow);
	m_info.isCollapsed = false;
    }
    else {
        collapseButton->setArrowType(Qt::RightArrow);
	m_info.isCollapsed = true;
    }
    m_effect.setAttribute("kdenlive_info", m_info.toString());
    emit parameterChanged(m_original_effect, m_effect, effectIndex());   
}

void CollapsibleEffect::updateGroupIndex(int groupIndex)
{
    m_info.groupIndex = groupIndex;
    m_effect.setAttribute("kdenlive_info", m_info.toString());
    emit parameterChanged(m_original_effect, m_effect, effectIndex());
}

void CollapsibleEffect::setGroupIndex(int ix)
{
    m_info.groupIndex = ix;
}

void CollapsibleEffect::addGroupEffect(CollapsibleEffect *effect)
{
    QVBoxLayout *vbox = static_cast<QVBoxLayout *>(widgetFrame->layout());
    if (vbox == NULL) {
	vbox = new QVBoxLayout();
	vbox->setContentsMargins(10, 0, 0, 0);
	vbox->setSpacing(2);
	widgetFrame->setLayout(vbox);
    }
    effect->setGroupIndex(groupIndex());
    vbox->addWidget(effect);
}

QString CollapsibleEffect::infoString() const
{
    return m_info.toString();
}

void CollapsibleEffect::removeGroup(int ix, QVBoxLayout *layout)
{
    QVBoxLayout *vbox = static_cast<QVBoxLayout *>(widgetFrame->layout());
    if (vbox == NULL) return;
    
    for (int j = vbox->count() - 1; j >= 0; j--) {
	QLayoutItem *child = vbox->takeAt(j);
	CollapsibleEffect *e = static_cast<CollapsibleEffect *>(child->widget());
	layout->insertWidget(ix, e);
	e->updateGroupIndex(-1);
	delete child;
    }
}

int CollapsibleEffect::index() const
{
    return m_index;
}

int CollapsibleEffect::groupIndex() const
{
    if (m_isGroup) return m_index;
    return -1;
}

bool CollapsibleEffect::isGroup() const
{
    return m_isGroup;
}

int CollapsibleEffect::effectIndex() const
{
    if (m_effect.isNull()) return -1;
    return m_effect.attribute("kdenlive_ix").toInt();
}

void CollapsibleEffect::updateWidget(ItemInfo info, int index, QDomElement effect, EffectMetaInfo *metaInfo)
{
    if (m_paramWidget) {
	// cleanup
	delete m_paramWidget;
	m_paramWidget = NULL;
    }
    m_effect = effect;
    setupWidget(info, index, metaInfo);
}

void CollapsibleEffect::setupWidget(ItemInfo info, int index, EffectMetaInfo *metaInfo)
{
    if (m_effect.isNull()) {
//         kDebug() << "// EMPTY EFFECT STACK";
        return;
    }

    if (m_effect.attribute("tag") == "region") {
        QVBoxLayout *vbox = new QVBoxLayout(widgetFrame);
        vbox->setContentsMargins(2, 0, 2, 0);
	vbox->setSpacing(2);
        QDomNodeList effects =  m_effect.elementsByTagName("effect");
	QDomNodeList origin_effects =  m_original_effect.elementsByTagName("effect");
        QWidget *container = new QWidget(widgetFrame);
        vbox->addWidget(container);
        m_paramWidget = new ParameterContainer(m_effect.toElement(), info, metaInfo, index, container);
        for (int i = 0; i < effects.count(); i++) {
            CollapsibleEffect *coll = new CollapsibleEffect(effects.at(i).toElement(), origin_effects.at(i).toElement(), info, i, metaInfo, container);
            m_subParamWidgets.append(coll);
            //container = new QWidget(widgetFrame);
            vbox->addWidget(coll);
            //p = new ParameterContainer(effects.at(i).toElement(), info, isEffect, container);
        }
        
    }
    else {
        m_paramWidget = new ParameterContainer(m_effect, info, metaInfo, index, widgetFrame);
        if (m_effect.firstChildElement("parameter").isNull()) {
            // Effect has no parameter, don't allow expand
            collapseButton->setEnabled(false);
	    collapseButton->setVisible(false);
            widgetFrame->setVisible(false);            
        }
    }
    if (collapseButton->isEnabled() && m_info.isCollapsed) {
 	widgetFrame->setVisible(false);
	collapseButton->setArrowType(Qt::RightArrow);
	
    }
    connect (m_paramWidget, SIGNAL(parameterChanged(const QDomElement, const QDomElement, int)), this, SIGNAL(parameterChanged(const QDomElement, const QDomElement, int)));
    
    connect(m_paramWidget, SIGNAL(startFilterJob(QString,QString,QString,QString,QString,QString)), this, SIGNAL(startFilterJob(QString,QString,QString,QString,QString,QString)));
    
    connect (this, SIGNAL(syncEffectsPos(int)), m_paramWidget, SIGNAL(syncEffectsPos(int)));
    connect (this, SIGNAL(effectStateChanged(bool)), m_paramWidget, SIGNAL(effectStateChanged(bool)));
    connect (m_paramWidget, SIGNAL(checkMonitorPosition(int)), this, SIGNAL(checkMonitorPosition(int)));
    connect (m_paramWidget, SIGNAL(seekTimeline(int)), this, SIGNAL(seekTimeline(int)));
    
    
}

void CollapsibleEffect::updateTimecodeFormat()
{
    m_paramWidget->updateTimecodeFormat();
    if (!m_subParamWidgets.isEmpty()) {
        // we have a group
        for (int i = 0; i < m_subParamWidgets.count(); i++)
            m_subParamWidgets.at(i)->updateTimecodeFormat();
    }
}

void CollapsibleEffect::slotSyncEffectsPos(int pos)
{
    emit syncEffectsPos(pos);
}

void CollapsibleEffect::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("kdenlive/effectslist")) {
	frame->setProperty("active", true);
	frame->setStyleSheet(frame->styleSheet());
	event->acceptProposedAction();
    }
}

void CollapsibleEffect::dragLeaveEvent(QDragLeaveEvent */*event*/)
{
    frame->setProperty("active", false);
    frame->setStyleSheet(frame->styleSheet());
}

void CollapsibleEffect::dropEvent(QDropEvent *event)
{
    frame->setProperty("active", false);
    frame->setStyleSheet(frame->styleSheet());
    const QString effects = QString::fromUtf8(event->mimeData()->data("kdenlive/effectslist"));
    //event->acceptProposedAction();
    QDomDocument doc;
    doc.setContent(effects, true);
    QDomElement e = doc.documentElement();
    int ix = e.attribute("kdenlive_ix").toInt();
    if (ix == effectIndex()) {
	// effect dropped on itself, reject
	event->ignore();
	return;
    }
    if (ix == 0) {
	// effect dropped from effects list, add it
	e.setAttribute("kdenlive_ix", ix);
	event->setDropAction(Qt::CopyAction);
	event->accept();
	emit addEffect(e);
	return;
    }
    int new_index = -1;
    if (m_isGroup) {
	QVBoxLayout *vbox = static_cast<QVBoxLayout *>(widgetFrame->layout());
	if (vbox == NULL) return;
	CollapsibleEffect *e = static_cast<CollapsibleEffect *>(vbox->itemAt(vbox->count() -1)->widget());
	new_index = e->effectIndex();
    }
    else {
	new_index = effectIndex();
    }
    emit moveEffect(ix, new_index, this);
    event->setDropAction(Qt::MoveAction);
    event->accept();
}

ParameterContainer::ParameterContainer(QDomElement effect, ItemInfo info, EffectMetaInfo *metaInfo, int index, QWidget * parent) :
	m_index(index),
        m_keyframeEditor(NULL),
        m_geometryWidget(NULL),
        m_metaInfo(metaInfo),
        m_effect(effect)
{
    m_in = info.cropStart.frames(KdenliveSettings::project_fps());
    m_out = (info.cropStart + info.cropDuration).frames(KdenliveSettings::project_fps()) - 1;

    QDomNodeList namenode = effect.childNodes(); //elementsByTagName("parameter");
    
    QDomElement e = effect.toElement();
    int minFrame = e.attribute("start").toInt();
    int maxFrame = e.attribute("end").toInt();
    // In transitions, maxFrame is in fact one frame after the end of transition
    if (maxFrame > 0) maxFrame --;

    bool disable = effect.attribute("disable") == "1" && KdenliveSettings::disable_effect_parameters();
    parent->setEnabled(!disable);

    bool stretch = true;
    m_vbox = new QVBoxLayout(parent);
    m_vbox->setContentsMargins(2, 0, 2, 0);
    m_vbox->setSpacing(2);

    for (int i = 0; i < namenode.count() ; i++) {
        QDomElement pa = namenode.item(i).toElement();
        if (pa.tagName() != "parameter") continue;
        QDomElement na = pa.firstChildElement("name");
        QDomElement commentElem = pa.firstChildElement("comment");
        QString type = pa.attribute("type");
        QString paramName = na.isNull() ? pa.attribute("name") : i18n(na.text().toUtf8().data());
        QString comment;
        if (!commentElem.isNull())
            comment = i18n(commentElem.text().toUtf8().data());
        QWidget * toFillin = new QWidget(parent);
        QString value = pa.attribute("value").isNull() ?
                        pa.attribute("default") : pa.attribute("value");


        /** See effects/README for info on the different types */

        if (type == "double" || type == "constant") {
            double min;
            double max;
            if (pa.attribute("min").contains('%'))
                min = ProfilesDialog::getStringEval(m_metaInfo->profile, pa.attribute("min"), m_metaInfo->frameSize);
            else
                min = pa.attribute("min").toDouble();
            if (pa.attribute("max").contains('%'))
                max = ProfilesDialog::getStringEval(m_metaInfo->profile, pa.attribute("max"), m_metaInfo->frameSize);
            else
                max = pa.attribute("max").toDouble();

            DoubleParameterWidget *doubleparam = new DoubleParameterWidget(paramName, value.toDouble(), min, max,
                    pa.attribute("default").toDouble(), comment, -1, pa.attribute("suffix"), pa.attribute("decimals").toInt(), parent);
	    doubleparam->setFocusPolicy(Qt::StrongFocus);
            m_vbox->addWidget(doubleparam);
            m_valueItems[paramName] = doubleparam;
            connect(doubleparam, SIGNAL(valueChanged(double)), this, SLOT(slotCollectAllParameters()));
            connect(this, SIGNAL(showComments(bool)), doubleparam, SLOT(slotShowComment(bool)));
        } else if (type == "list") {
            Listval *lsval = new Listval;
            lsval->setupUi(toFillin);
	    lsval->list->setFocusPolicy(Qt::StrongFocus);
            QStringList listitems = pa.attribute("paramlist").split(';');
            if (listitems.count() == 1) {
                // probably custom effect created before change to ';' as separator
                listitems = pa.attribute("paramlist").split(",");
            }
            QDomElement list = pa.firstChildElement("paramlistdisplay");
            QStringList listitemsdisplay;
            if (!list.isNull()) {
                listitemsdisplay = i18n(list.text().toUtf8().data()).split(',');
            } else {
                listitemsdisplay = i18n(pa.attribute("paramlistdisplay").toUtf8().data()).split(',');
            }
            if (listitemsdisplay.count() != listitems.count())
                listitemsdisplay = listitems;
            lsval->list->setIconSize(QSize(30, 30));
            for (int i = 0; i < listitems.count(); i++) {
                lsval->list->addItem(listitemsdisplay.at(i), listitems.at(i));
                QString entry = listitems.at(i);
                if (!entry.isEmpty() && (entry.endsWith(".png") || entry.endsWith(".pgm"))) {
                    if (!CollapsibleEffect::iconCache.contains(entry)) {
                        QImage pix(entry);
                        CollapsibleEffect::iconCache[entry] = pix.scaled(30, 30);
                    }
                    lsval->list->setItemIcon(i, QPixmap::fromImage(CollapsibleEffect::iconCache[entry]));
                }
            }
            if (!value.isEmpty()) lsval->list->setCurrentIndex(listitems.indexOf(value));
            lsval->name->setText(paramName);
            lsval->labelComment->setText(comment);
            lsval->widgetComment->setHidden(true);
            m_valueItems[paramName] = lsval;
            connect(lsval->list, SIGNAL(currentIndexChanged(int)) , this, SLOT(slotCollectAllParameters()));
            if (!comment.isEmpty())
                connect(this, SIGNAL(showComments(bool)), lsval->widgetComment, SLOT(setVisible(bool)));
            m_uiItems.append(lsval);
        } else if (type == "bool") {
            Boolval *bval = new Boolval;
            bval->setupUi(toFillin);
            bval->checkBox->setCheckState(value == "0" ? Qt::Unchecked : Qt::Checked);
            bval->name->setText(paramName);
            bval->labelComment->setText(comment);
            bval->widgetComment->setHidden(true);
            m_valueItems[paramName] = bval;
            connect(bval->checkBox, SIGNAL(stateChanged(int)) , this, SLOT(slotCollectAllParameters()));
            if (!comment.isEmpty())
                connect(this, SIGNAL(showComments(bool)), bval->widgetComment, SLOT(setVisible(bool)));
            m_uiItems.append(bval);
        } else if (type == "complex") {
            ComplexParameter *pl = new ComplexParameter;
            pl->setupParam(effect, pa.attribute("name"), 0, 100);
            m_vbox->addWidget(pl);
            m_valueItems[paramName+"complex"] = pl;
            connect(pl, SIGNAL(parameterChanged()), this, SLOT(slotCollectAllParameters()));
        } else if (type == "geometry") {
            if (KdenliveSettings::on_monitor_effects()) {
                m_geometryWidget = new GeometryWidget(m_metaInfo->monitor, m_metaInfo->timecode, 0, true, effect.hasAttribute("showrotation"), parent);
                m_geometryWidget->setFrameSize(m_metaInfo->frameSize);
                m_geometryWidget->slotShowScene(!disable);
                // connect this before setupParam to make sure the monitor scene shows up at startup
                connect(m_geometryWidget, SIGNAL(checkMonitorPosition(int)), this, SIGNAL(checkMonitorPosition(int)));
                connect(m_geometryWidget, SIGNAL(parameterChanged()), this, SLOT(slotCollectAllParameters()));
                if (minFrame == maxFrame)
                    m_geometryWidget->setupParam(pa, m_in, m_out);
                else
                    m_geometryWidget->setupParam(pa, minFrame, maxFrame);
                m_vbox->addWidget(m_geometryWidget);
                m_valueItems[paramName+"geometry"] = m_geometryWidget;
                connect(m_geometryWidget, SIGNAL(seekToPos(int)), this, SIGNAL(seekTimeline(int)));
                connect(this, SIGNAL(syncEffectsPos(int)), m_geometryWidget, SLOT(slotSyncPosition(int)));
                connect(this, SIGNAL(effectStateChanged(bool)), m_geometryWidget, SLOT(slotShowScene(bool)));
            } else {
                Geometryval *geo = new Geometryval(m_metaInfo->profile, m_metaInfo->timecode, m_metaInfo->frameSize, 0);
                if (minFrame == maxFrame)
                    geo->setupParam(pa, m_in, m_out);
                else
                    geo->setupParam(pa, minFrame, maxFrame);
                m_vbox->addWidget(geo);
                m_valueItems[paramName+"geometry"] = geo;
                connect(geo, SIGNAL(parameterChanged()), this, SLOT(slotCollectAllParameters()));
                connect(geo, SIGNAL(seekToPos(int)), this, SIGNAL(seekTimeline(int)));
                connect(this, SIGNAL(syncEffectsPos(int)), geo, SLOT(slotSyncPosition(int)));
            }
        } else if (type == "addedgeometry") {
            // this is a parameter that should be linked to the geometry widget, for example rotation, shear, ...
            if (m_geometryWidget) m_geometryWidget->addParameter(pa);
        } else if (type == "keyframe" || type == "simplekeyframe") {
            // keyframe editor widget
            if (m_keyframeEditor == NULL) {
                KeyframeEdit *geo;
                if (pa.attribute("widget") == "corners") {
                    // we want a corners-keyframe-widget
                    CornersWidget *corners = new CornersWidget(m_metaInfo->monitor, pa, m_in, m_out, m_metaInfo->timecode, e.attribute("active_keyframe", "-1").toInt(), parent);
                    corners->slotShowScene(!disable);
                    connect(corners, SIGNAL(checkMonitorPosition(int)), this, SIGNAL(checkMonitorPosition(int)));
                    connect(this, SIGNAL(effectStateChanged(bool)), corners, SLOT(slotShowScene(bool)));
                    connect(this, SIGNAL(syncEffectsPos(int)), corners, SLOT(slotSyncPosition(int)));
                    geo = static_cast<KeyframeEdit *>(corners);
                } else {
                    geo = new KeyframeEdit(pa, m_in, m_out, m_metaInfo->timecode, e.attribute("active_keyframe", "-1").toInt());
                }
                m_vbox->addWidget(geo);
                m_valueItems[paramName+"keyframe"] = geo;
                m_keyframeEditor = geo;
                connect(geo, SIGNAL(parameterChanged()), this, SLOT(slotCollectAllParameters()));
                connect(geo, SIGNAL(seekToPos(int)), this, SIGNAL(seekTimeline(int)));
                connect(this, SIGNAL(showComments(bool)), geo, SIGNAL(showComments(bool)));
            } else {
                // we already have a keyframe editor, so just add another column for the new param
                m_keyframeEditor->addParameter(pa);
            }
        } else if (type == "color") {
            if (value.startsWith('#'))
                value = value.replace('#', "0x");
            ChooseColorWidget *choosecolor = new ChooseColorWidget(paramName, value, parent);
            m_vbox->addWidget(choosecolor);
            m_valueItems[paramName] = choosecolor;
            connect(choosecolor, SIGNAL(displayMessage(const QString&, int)), this, SIGNAL(displayMessage(const QString&, int)));
            connect(choosecolor, SIGNAL(modified()) , this, SLOT(slotCollectAllParameters()));
        } else if (type == "position") {
            int pos = value.toInt();
            if (effect.attribute("id") == "fadein" || effect.attribute("id") == "fade_from_black") {
                pos = pos - m_in;
            } else if (effect.attribute("id") == "fadeout" || effect.attribute("id") == "fade_to_black") {
                // fadeout position starts from clip end
                pos = m_out - pos;
            }
            PositionEdit *posedit = new PositionEdit(paramName, pos, 0, m_out - m_in, m_metaInfo->timecode);
            m_vbox->addWidget(posedit);
            m_valueItems[paramName+"position"] = posedit;
            connect(posedit, SIGNAL(parameterChanged()), this, SLOT(slotCollectAllParameters()));
        } else if (type == "curve") {
            KisCurveWidget *curve = new KisCurveWidget(parent);
            curve->setMaxPoints(pa.attribute("max").toInt());
            QList<QPointF> points;
            int number = EffectsList::parameter(e, pa.attribute("number")).toInt();
            QString inName = pa.attribute("inpoints");
            QString outName = pa.attribute("outpoints");
            int start = pa.attribute("min").toInt();
            for (int j = start; j <= number; j++) {
                QString in = inName;
                in.replace("%i", QString::number(j));
                QString out = outName;
                out.replace("%i", QString::number(j));
                points << QPointF(EffectsList::parameter(e, in).toDouble(), EffectsList::parameter(e, out).toDouble());
            }
            if (!points.isEmpty())
                curve->setCurve(KisCubicCurve(points));
            MySpinBox *spinin = new MySpinBox();
            spinin->setRange(0, 1000);
            MySpinBox *spinout = new MySpinBox();
            spinout->setRange(0, 1000);
            curve->setupInOutControls(spinin, spinout, 0, 1000);
            m_vbox->addWidget(curve);
            m_vbox->addWidget(spinin);
            m_vbox->addWidget(spinout);

            connect(curve, SIGNAL(modified()), this, SLOT(slotCollectAllParameters()));
            m_valueItems[paramName] = curve;

            QString depends = pa.attribute("depends");
            if (!depends.isEmpty())
                meetDependency(paramName, type, EffectsList::parameter(e, depends));
        } else if (type == "bezier_spline") {
            BezierSplineWidget *widget = new BezierSplineWidget(value, parent);
            stretch = false;
            m_vbox->addWidget(widget);
            m_valueItems[paramName] = widget;
            connect(widget, SIGNAL(modified()), this, SLOT(slotCollectAllParameters()));
            QString depends = pa.attribute("depends");
            if (!depends.isEmpty())
                meetDependency(paramName, type, EffectsList::parameter(e, depends));
#ifdef USE_QJSON
        } else if (type == "roto-spline") {
            RotoWidget *roto = new RotoWidget(value, m_metaInfo->monitor, info, m_metaInfo->timecode, parent);
            roto->slotShowScene(!disable);
            connect(roto, SIGNAL(valueChanged()), this, SLOT(slotCollectAllParameters()));
            connect(roto, SIGNAL(checkMonitorPosition(int)), this, SIGNAL(checkMonitorPosition(int)));
            connect(roto, SIGNAL(seekToPos(int)), this, SIGNAL(seekTimeline(int)));
            connect(this, SIGNAL(syncEffectsPos(int)), roto, SLOT(slotSyncPosition(int)));
            connect(this, SIGNAL(effectStateChanged(bool)), roto, SLOT(slotShowScene(bool)));
            m_vbox->addWidget(roto);
            m_valueItems[paramName] = roto;
#endif
        } else if (type == "wipe") {
            Wipeval *wpval = new Wipeval;
            wpval->setupUi(toFillin);
            wipeInfo w = getWipeInfo(value);
            switch (w.start) {
            case UP:
                wpval->start_up->setChecked(true);
                break;
            case DOWN:
                wpval->start_down->setChecked(true);
                break;
            case RIGHT:
                wpval->start_right->setChecked(true);
                break;
            case LEFT:
                wpval->start_left->setChecked(true);
                break;
            default:
                wpval->start_center->setChecked(true);
                break;
            }
            switch (w.end) {
            case UP:
                wpval->end_up->setChecked(true);
                break;
            case DOWN:
                wpval->end_down->setChecked(true);
                break;
            case RIGHT:
                wpval->end_right->setChecked(true);
                break;
            case LEFT:
                wpval->end_left->setChecked(true);
                break;
            default:
                wpval->end_center->setChecked(true);
                break;
            }
            wpval->start_transp->setValue(w.startTransparency);
            wpval->end_transp->setValue(w.endTransparency);
            m_valueItems[paramName] = wpval;
            connect(wpval->end_up, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->end_down, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->end_left, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->end_right, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->end_center, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->start_up, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->start_down, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->start_left, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->start_right, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->start_center, SIGNAL(clicked()), this, SLOT(slotCollectAllParameters()));
            connect(wpval->start_transp, SIGNAL(valueChanged(int)), this, SLOT(slotCollectAllParameters()));
            connect(wpval->end_transp, SIGNAL(valueChanged(int)), this, SLOT(slotCollectAllParameters()));
            //wpval->title->setTitle(na.toElement().text());
            m_uiItems.append(wpval);
        } else if (type == "url") {
            Urlval *cval = new Urlval;
            cval->setupUi(toFillin);
            cval->label->setText(paramName);
            cval->urlwidget->fileDialog()->setFilter(ProjectList::getExtensions());
            m_valueItems[paramName] = cval;
            cval->urlwidget->setUrl(KUrl(value));
            connect(cval->urlwidget, SIGNAL(returnPressed()) , this, SLOT(slotCollectAllParameters()));
            connect(cval->urlwidget, SIGNAL(urlSelected(const KUrl&)) , this, SLOT(slotCollectAllParameters()));
            m_uiItems.append(cval);
	} else if (type == "keywords") {
            Keywordval* kval = new Keywordval;
            kval->setupUi(toFillin);
            kval->label->setText(paramName);
            kval->lineeditwidget->setText(value);
            QDomElement klistelem = pa.firstChildElement("keywords");
            QDomElement kdisplaylistelem = pa.firstChildElement("keywordsdisplay");
            QStringList keywordlist;
            QStringList keyworddisplaylist;
            if (!klistelem.isNull()) {
                keywordlist = klistelem.text().split(';');
                keyworddisplaylist = i18n(kdisplaylistelem.text().toUtf8().data()).split(';');
            }
            if (keyworddisplaylist.count() != keywordlist.count()) {
                keyworddisplaylist = keywordlist;
            }
            for (int i = 0; i < keywordlist.count(); i++) {
                kval->comboboxwidget->addItem(keyworddisplaylist.at(i), keywordlist.at(i));
            }
            // Add disabled user prompt at index 0
            kval->comboboxwidget->insertItem(0, i18n("<select a keyword>"), "");
            kval->comboboxwidget->model()->setData( kval->comboboxwidget->model()->index(0,0), QVariant(Qt::NoItemFlags), Qt::UserRole -1);
            kval->comboboxwidget->setCurrentIndex(0);
            m_valueItems[paramName] = kval;
            connect(kval->lineeditwidget, SIGNAL(editingFinished()) , this, SLOT(collectAllParameters()));
            connect(kval->comboboxwidget, SIGNAL(activated (const QString&)), this, SLOT(collectAllParameters()));
            m_uiItems.append(kval);
        } else if (type == "fontfamily") {
            Fontval* fval = new Fontval;
            fval->setupUi(toFillin);
            fval->name->setText(paramName);
            fval->fontfamilywidget->setCurrentFont(QFont(value));
            m_valueItems[paramName] = fval;
            connect(fval->fontfamilywidget, SIGNAL(currentFontChanged(const QFont &)), this, SLOT(collectAllParameters())) ;
            m_uiItems.append(fval);
        } else if (type == "filterjob") {
	    QVBoxLayout *l= new QVBoxLayout(toFillin);
	    QPushButton *button = new QPushButton(paramName, toFillin);
	    l->addWidget(button);
            m_valueItems[paramName] = button;
            connect(button, SIGNAL(pressed()), this, SLOT(slotStartFilterJobAction()));   
        } else {
            delete toFillin;
            toFillin = NULL;
        }

        if (toFillin)
            m_vbox->addWidget(toFillin);
    }

    if (stretch)
        m_vbox->addStretch();

    if (m_keyframeEditor)
        m_keyframeEditor->checkVisibleParam();

    // Make sure all doubleparam spinboxes have the same width, looks much better
    QList<DoubleParameterWidget *> allWidgets = findChildren<DoubleParameterWidget *>();
    int minSize = 0;
    for (int i = 0; i < allWidgets.count(); i++) {
        if (minSize < allWidgets.at(i)->spinSize()) minSize = allWidgets.at(i)->spinSize();
    }
    for (int i = 0; i < allWidgets.count(); i++) {
        allWidgets.at(i)->setSpinSize(minSize);
    }
}

ParameterContainer::~ParameterContainer()
{
    clearLayout(m_vbox);
    delete m_vbox;
}

void ParameterContainer::meetDependency(const QString& name, QString type, QString value)
{
    if (type == "curve") {
        KisCurveWidget *curve = (KisCurveWidget*)m_valueItems[name];
        if (curve) {
            int color = value.toInt();
            curve->setPixmap(QPixmap::fromImage(ColorTools::rgbCurvePlane(curve->size(), (ColorTools::ColorsRGB)(color == 3 ? 4 : color), 0.8)));
        }
    } else if (type == "bezier_spline") {
        BezierSplineWidget *widget = (BezierSplineWidget*)m_valueItems[name];
        if (widget) {
            widget->setMode((BezierSplineWidget::CurveModes)((int)(value.toDouble() * 10)));
        }
    }
}

wipeInfo ParameterContainer::getWipeInfo(QString value)
{
    wipeInfo info;
    // Convert old geometry values that used a comma as separator
    if (value.contains(',')) value.replace(',','/');
    QString start = value.section(';', 0, 0);
    QString end = value.section(';', 1, 1).section('=', 1, 1);
    if (start.startsWith("-100%/0"))
        info.start = LEFT;
    else if (start.startsWith("100%/0"))
        info.start = RIGHT;
    else if (start.startsWith("0%/100%"))
        info.start = DOWN;
    else if (start.startsWith("0%/-100%"))
        info.start = UP;
    else
        info.start = CENTER;

    if (start.count(':') == 2)
        info.startTransparency = start.section(':', -1).toInt();
    else
        info.startTransparency = 100;

    if (end.startsWith("-100%/0"))
        info.end = LEFT;
    else if (end.startsWith("100%/0"))
        info.end = RIGHT;
    else if (end.startsWith("0%/100%"))
        info.end = DOWN;
    else if (end.startsWith("0%/-100%"))
        info.end = UP;
    else
        info.end = CENTER;

    if (end.count(':') == 2)
        info.endTransparency = end.section(':', -1).toInt();
    else
        info.endTransparency = 100;

    return info;
}

void ParameterContainer::updateTimecodeFormat()
{
    if (m_keyframeEditor)
        m_keyframeEditor->updateTimecodeFormat();

    QDomNodeList namenode = m_effect.elementsByTagName("parameter");
    for (int i = 0; i < namenode.count() ; i++) {
        QDomNode pa = namenode.item(i);
        QDomElement na = pa.firstChildElement("name");
        QString type = pa.attributes().namedItem("type").nodeValue();
        QString paramName = na.isNull() ? pa.attributes().namedItem("name").nodeValue() : i18n(na.text().toUtf8().data());

        if (type == "geometry") {
            if (KdenliveSettings::on_monitor_effects()) {
                if (m_geometryWidget) m_geometryWidget->updateTimecodeFormat();
            } else {
                Geometryval *geom = ((Geometryval*)m_valueItems[paramName+"geometry"]);
                geom->updateTimecodeFormat();
            }
            break;
        } else if (type == "position") {
            PositionEdit *posi = ((PositionEdit*)m_valueItems[paramName+"position"]);
            posi->updateTimecodeFormat();
            break;
#ifdef USE_QJSON
        } else if (type == "roto-spline") {
            RotoWidget *widget = static_cast<RotoWidget *>(m_valueItems[paramName]);
            widget->updateTimecodeFormat();
#endif
        }
    }
}

void ParameterContainer::slotCollectAllParameters()
{
    if (m_valueItems.isEmpty() || m_effect.isNull()) return;
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    const QDomElement oldparam = m_effect.cloneNode().toElement();
    //QDomElement newparam = oldparam.cloneNode().toElement();
    QDomNodeList namenode = m_effect.elementsByTagName("parameter");

    for (int i = 0; i < namenode.count() ; i++) {
        QDomNode pa = namenode.item(i);
        QDomElement na = pa.firstChildElement("name");
        QString type = pa.attributes().namedItem("type").nodeValue();
        QString paramName = na.isNull() ? pa.attributes().namedItem("name").nodeValue() : i18n(na.text().toUtf8().data());
        if (type == "complex")
            paramName.append("complex");
        else if (type == "position")
            paramName.append("position");
        else if (type == "geometry")
            paramName.append("geometry");
        else if (type == "keyframe")
            paramName.append("keyframe");
        if (type != "simplekeyframe" && type != "fixed" && type != "addedgeometry" && !m_valueItems.contains(paramName)) {
            kDebug() << "// Param: " << paramName << " NOT FOUND";
            continue;
        }

        QString setValue;
        if (type == "double" || type == "constant") {
            DoubleParameterWidget *doubleparam = (DoubleParameterWidget*)m_valueItems.value(paramName);
            setValue = locale.toString(doubleparam->getValue());
        } else if (type == "list") {
            KComboBox *box = ((Listval*)m_valueItems.value(paramName))->list;
            setValue = box->itemData(box->currentIndex()).toString();
        } else if (type == "bool") {
            QCheckBox *box = ((Boolval*)m_valueItems.value(paramName))->checkBox;
            setValue = box->checkState() == Qt::Checked ? "1" : "0" ;
        } else if (type == "color") {
            ChooseColorWidget *choosecolor = ((ChooseColorWidget*)m_valueItems.value(paramName));
            setValue = choosecolor->getColor();
        } else if (type == "complex") {
            ComplexParameter *complex = ((ComplexParameter*)m_valueItems.value(paramName));
            namenode.item(i) = complex->getParamDesc();
        } else if (type == "geometry") {
            if (KdenliveSettings::on_monitor_effects()) {
                if (m_geometryWidget) namenode.item(i).toElement().setAttribute("value", m_geometryWidget->getValue());
            } else {
                Geometryval *geom = ((Geometryval*)m_valueItems.value(paramName));
                namenode.item(i).toElement().setAttribute("value", geom->getValue());
            }
        } else if (type == "addedgeometry") {
            namenode.item(i).toElement().setAttribute("value", m_geometryWidget->getExtraValue(namenode.item(i).toElement().attribute("name")));
        } else if (type == "position") {
            PositionEdit *pedit = ((PositionEdit*)m_valueItems.value(paramName));
            int pos = pedit->getPosition();
            setValue = QString::number(pos);
            if (m_effect.attribute("id") == "fadein" || m_effect.attribute("id") == "fade_from_black") {
                // Make sure duration is not longer than clip
                /*if (pos > m_out) {
                    pos = m_out;
                    pedit->setPosition(pos);
                }*/
                EffectsList::setParameter(m_effect, "in", QString::number(m_in));
                EffectsList::setParameter(m_effect, "out", QString::number(m_in + pos));
                setValue.clear();
            } else if (m_effect.attribute("id") == "fadeout" || m_effect.attribute("id") == "fade_to_black") {
                // Make sure duration is not longer than clip
                /*if (pos > m_out) {
                    pos = m_out;
                    pedit->setPosition(pos);
                }*/
                EffectsList::setParameter(m_effect, "in", QString::number(m_out - pos));
                EffectsList::setParameter(m_effect, "out", QString::number(m_out));
                setValue.clear();
            }
        } else if (type == "curve") {
            KisCurveWidget *curve = ((KisCurveWidget*)m_valueItems.value(paramName));
            QList<QPointF> points = curve->curve().points();
            QString number = pa.attributes().namedItem("number").nodeValue();
            QString inName = pa.attributes().namedItem("inpoints").nodeValue();
            QString outName = pa.attributes().namedItem("outpoints").nodeValue();
            int off = pa.attributes().namedItem("min").nodeValue().toInt();
            int end = pa.attributes().namedItem("max").nodeValue().toInt();
            EffectsList::setParameter(m_effect, number, QString::number(points.count()));
            for (int j = 0; (j < points.count() && j + off <= end); j++) {
                QString in = inName;
                in.replace("%i", QString::number(j + off));
                QString out = outName;
                out.replace("%i", QString::number(j + off));
                EffectsList::setParameter(m_effect, in, locale.toString(points.at(j).x()));
                EffectsList::setParameter(m_effect, out, locale.toString(points.at(j).y()));
            }
            QString depends = pa.attributes().namedItem("depends").nodeValue();
            if (!depends.isEmpty())
                meetDependency(paramName, type, EffectsList::parameter(m_effect, depends));
        } else if (type == "bezier_spline") {
            BezierSplineWidget *widget = (BezierSplineWidget*)m_valueItems.value(paramName);
            setValue = widget->spline();
            QString depends = pa.attributes().namedItem("depends").nodeValue();
            if (!depends.isEmpty())
                meetDependency(paramName, type, EffectsList::parameter(m_effect, depends));
#ifdef USE_QJSON
        } else if (type == "roto-spline") {
            RotoWidget *widget = static_cast<RotoWidget *>(m_valueItems.value(paramName));
            setValue = widget->getSpline();
#endif
        } else if (type == "wipe") {
            Wipeval *wp = (Wipeval*)m_valueItems.value(paramName);
            wipeInfo info;
            if (wp->start_left->isChecked())
                info.start = LEFT;
            else if (wp->start_right->isChecked())
                info.start = RIGHT;
            else if (wp->start_up->isChecked())
                info.start = UP;
            else if (wp->start_down->isChecked())
                info.start = DOWN;
            else if (wp->start_center->isChecked())
                info.start = CENTER;
            else
                info.start = LEFT;
            info.startTransparency = wp->start_transp->value();

            if (wp->end_left->isChecked())
                info.end = LEFT;
            else if (wp->end_right->isChecked())
                info.end = RIGHT;
            else if (wp->end_up->isChecked())
                info.end = UP;
            else if (wp->end_down->isChecked())
                info.end = DOWN;
            else if (wp->end_center->isChecked())
                info.end = CENTER;
            else
                info.end = RIGHT;
            info.endTransparency = wp->end_transp->value();

            setValue = getWipeString(info);
        } else if ((type == "simplekeyframe" || type == "keyframe") && m_keyframeEditor) {
            QDomElement elem = pa.toElement();
            QString realName = i18n(na.toElement().text().toUtf8().data());
            QString val = m_keyframeEditor->getValue(realName);
            elem.setAttribute("keyframes", val);

            if (m_keyframeEditor->isVisibleParam(realName))
                elem.setAttribute("intimeline", "1");
            else if (elem.hasAttribute("intimeline"))
                elem.removeAttribute("intimeline");
        } else if (type == "url") {
            KUrlRequester *req = ((Urlval*)m_valueItems.value(paramName))->urlwidget;
            setValue = req->url().path();
	} else if (type == "keywords"){
            QLineEdit *line = ((Keywordval*)m_valueItems.value(paramName))->lineeditwidget;
            QComboBox *combo = ((Keywordval*)m_valueItems.value(paramName))->comboboxwidget;
            if(combo->currentIndex())
            {
                QString comboval = combo->itemData(combo->currentIndex()).toString();
                line->insert(comboval);
                combo->setCurrentIndex(0);
            }
            setValue = line->text();
        } else if (type == "fontfamily") {
            QFontComboBox* fontfamily = ((Fontval*)m_valueItems.value(paramName))->fontfamilywidget;
            setValue = fontfamily->currentFont().family();
        }
        if (!setValue.isNull())
            pa.attributes().namedItem("value").setNodeValue(setValue);

    }
    emit parameterChanged(oldparam, m_effect, m_effect.attribute("kdenlive_ix").toInt());
}

QString ParameterContainer::getWipeString(wipeInfo info)
{

    QString start;
    QString end;
    switch (info.start) {
    case LEFT:
        start = "-100%/0%:100%x100%";
        break;
    case RIGHT:
        start = "100%/0%:100%x100%";
        break;
    case DOWN:
        start = "0%/100%:100%x100%";
        break;
    case UP:
        start = "0%/-100%:100%x100%";
        break;
    default:
        start = "0%/0%:100%x100%";
        break;
    }
    start.append(':' + QString::number(info.startTransparency));

    switch (info.end) {
    case LEFT:
        end = "-100%/0%:100%x100%";
        break;
    case RIGHT:
        end = "100%/0%:100%x100%";
        break;
    case DOWN:
        end = "0%/100%:100%x100%";
        break;
    case UP:
        end = "0%/-100%:100%x100%";
        break;
    default:
        end = "0%/0%:100%x100%";
        break;
    }
    end.append(':' + QString::number(info.endTransparency));
    return QString(start + ";-1=" + end);
}

int ParameterContainer::index() const
{
    return m_index;
}

void ParameterContainer::slotStartFilterJobAction()
{
    QDomNodeList namenode = m_effect.elementsByTagName("parameter");
    for (int i = 0; i < namenode.count() ; i++) {
        QDomElement pa = namenode.item(i).toElement();
        QString type = pa.attribute("type");
        if (type == "filterjob") {
            emit startFilterJob(pa.attribute("filtertag"), pa.attribute("filterparams"), pa.attribute("finalfilter"), pa.attribute("consumer"), pa.attribute("consumerparams"), pa.attribute("wantedproperties"));
            kDebug()<<" - - -PROPS:\n"<<pa.attribute("filtertag")<<"-"<< pa.attribute("filterparams")<<"-"<< pa.attribute("consumer")<<"-"<< pa.attribute("consumerparams")<<"-"<< pa.attribute("wantedproperties");
            break;
        }
    }
}


