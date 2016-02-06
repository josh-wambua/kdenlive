/***************************************************************************
 *   Copyright (C) 2008 by Marco Gittler (g.marco@freenet.de)              *
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

#include "abstractclipitem.h"
#include "customtrackscene.h"
#include "kdenlivesettings.h"
#include "customtrackview.h"

#include "mlt++/Mlt.h"

#include <QDebug>
#include <klocalizedstring.h>

#include <QApplication>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>

AbstractClipItem::AbstractClipItem(const ItemInfo &info, const QRectF& rect, double fps) :
        QObject()
        , QGraphicsRectItem(rect)
        , m_info(info)
        , m_visibleParam(0)
        , m_fps(fps)
        , m_isMainSelectedClip(false)
	, m_keyframeView(QFontInfo(QApplication::font()).pixelSize() * 0.7, this)
{
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    setPen(Qt::NoPen);
    connect(&m_keyframeView, SIGNAL(updateKeyframes(const QRectF&)), this, SLOT(doUpdate(const QRectF&)));
}

AbstractClipItem::~AbstractClipItem()
{
}

void AbstractClipItem::doUpdate(const QRectF &r)
{
    update(r);
}

void AbstractClipItem::updateKeyFramePos(int frame, const double y)
{
    m_keyframeView.updateKeyFramePos(rect(), frame, qBound(0.0, y, rect().height()));
}

int AbstractClipItem::editedKeyFramePos() const
{
    return m_keyframeView.editedKeyFramePos();
}

int AbstractClipItem::selectedKeyFramePos() const
{
    return m_keyframeView.selectedKeyFramePos();
}

void AbstractClipItem::updateSelectedKeyFrame()
{
    m_keyframeView.updateSelectedKeyFrame(rect());
}

int AbstractClipItem::keyframesCount()
{
    return m_keyframeView.keyframesCount();
}

double AbstractClipItem::editedKeyFrameValue()
{
    return m_keyframeView.editedKeyFrameValue();
}

double AbstractClipItem::getKeyFrameClipHeight(const double y)
{
    return m_keyframeView.getKeyFrameClipHeight(rect(), y);
}

void AbstractClipItem::closeAnimation()
{
    if (!isEnabled()) return;
    setEnabled(false);
    setFlag(QGraphicsItem::ItemIsSelectable, false);
    if (QApplication::style()->styleHint(QStyle::SH_Widget_Animate, 0, QApplication::activeWindow())) {
        // animation disabled
        deleteLater();
        return;
    }
    QPropertyAnimation *closeAnimation = new QPropertyAnimation(this, "rect");
    QPropertyAnimation *closeAnimation2 = new QPropertyAnimation(this, "opacity");
    closeAnimation->setDuration(200);
    closeAnimation2->setDuration(200);
    QRectF r = rect();
    QRectF r2 = r;
    r2.setLeft(r.left() + r.width() / 2);
    r2.setTop(r.top() + r.height() / 2);
    r2.setWidth(1);
    r2.setHeight(1);
    closeAnimation->setStartValue(r);
    closeAnimation->setEndValue(r2);
    closeAnimation->setEasingCurve(QEasingCurve::InQuad);
    closeAnimation2->setStartValue(1.0);
    closeAnimation2->setEndValue(0.0);
    QParallelAnimationGroup *group = new QParallelAnimationGroup;
    connect(group, SIGNAL(finished()), this, SLOT(deleteLater()));
    group->addAnimation(closeAnimation);
    group->addAnimation(closeAnimation2);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

ItemInfo AbstractClipItem::info() const
{
    ItemInfo info = m_info;
    info.cropStart = cropStart();
    info.endPos = endPos();
    return info;
}

GenTime AbstractClipItem::endPos() const
{
    return m_info.startPos + m_info.cropDuration;
}

int AbstractClipItem::track() const
{
    return m_info.track;
}

GenTime AbstractClipItem::cropStart() const
{
    return m_info.cropStart;
}

GenTime AbstractClipItem::cropDuration() const
{
    return m_info.cropDuration;
}

void AbstractClipItem::setCropStart(const GenTime &pos)
{
    m_info.cropStart = pos;
}

void AbstractClipItem::updateItem(int track)
{
    m_info.track = track;
    m_info.startPos = GenTime((int) scenePos().x(), m_fps);
}

void AbstractClipItem::updateRectGeometry()
{
    setRect(0, 0, cropDuration().frames(m_fps) - 0.02, rect().height());
}

void AbstractClipItem::resizeStart(int posx, bool hasSizeLimit, bool /*emitChange*/)
{
    GenTime durationDiff = GenTime(posx, m_fps) - m_info.startPos;
    if (durationDiff == GenTime()) return;

    if (type() == AVWidget && hasSizeLimit && (cropStart() + durationDiff < GenTime())) {
        durationDiff = GenTime() - cropStart();
    } else if (durationDiff >= cropDuration()) {
        return;
    }
    m_info.startPos += durationDiff;

    // set to true if crop from start is negative (possible for color clips, images as they have no size limit)
    bool negCropStart = false;
    if (type() == AVWidget) {
        m_info.cropStart += durationDiff;
        if (m_info.cropStart < GenTime())
            negCropStart = true;
    }

    m_info.cropDuration -= durationDiff;
    setRect(0, 0, cropDuration().frames(m_fps) - 0.02, rect().height());
    moveBy(durationDiff.frames(m_fps), 0);

    if (m_info.startPos != GenTime(posx, m_fps)) {
        GenTime diff = m_info.startPos - GenTime(posx, m_fps);

        if (type() == AVWidget)
            m_info.cropStart += diff;

        m_info.cropDuration -= diff;
        setRect(0, 0, cropDuration().frames(m_fps) - 0.02, rect().height());
    }

    // set crop from start to 0 (isn't relevant as this only happens for color clips, images)
    if (negCropStart)
        m_info.cropStart = GenTime();
}

void AbstractClipItem::resizeEnd(int posx, bool /*emitChange*/)
{
    GenTime durationDiff = GenTime(posx, m_fps) - endPos();
    if (durationDiff == GenTime()) return;
    if (cropDuration() + durationDiff <= GenTime()) {
        durationDiff = GenTime() - (cropDuration() - GenTime(3, m_fps));
    }

    m_info.cropDuration += durationDiff;
    m_info.endPos += durationDiff;

    setRect(0, 0, cropDuration().frames(m_fps) - 0.02, rect().height());
    if (durationDiff > GenTime()) {
        QList <QGraphicsItem *> collisionList = collidingItems(Qt::IntersectsItemBoundingRect);
        bool fixItem = false;
        for (int i = 0; i < collisionList.size(); ++i) {
            if (!collisionList.at(i)->isEnabled()) continue;
            QGraphicsItem *item = collisionList.at(i);
            if (item->type() == type() && item->pos().x() > pos().x()) {
                GenTime diff = static_cast<AbstractClipItem*>(item)->startPos() - startPos();
                if (fixItem == false || diff < m_info.cropDuration) {
                    fixItem = true;
                    m_info.cropDuration = diff;
                }
            }
        }
        if (fixItem) setRect(0, 0, cropDuration().frames(m_fps) - 0.02, rect().height());
    }
}

GenTime AbstractClipItem::startPos() const
{
    return m_info.startPos;
}

double AbstractClipItem::fps() const
{
    return m_fps;
}

void AbstractClipItem::updateFps(double fps)
{
    m_fps = fps;
    setPos((qreal) startPos().frames(m_fps), pos().y());
    updateRectGeometry();
}

GenTime AbstractClipItem::maxDuration() const
{
    return m_maxDuration;
}

CustomTrackScene* AbstractClipItem::projectScene()
{
    if (scene())
        return static_cast <CustomTrackScene*>(scene());
    return NULL;
}

void AbstractClipItem::setItemLocked(bool locked)
{
    if (locked)
        setSelected(false);

    setFlag(QGraphicsItem::ItemIsMovable, !locked);
    setFlag(QGraphicsItem::ItemIsSelectable, !locked);
}

bool AbstractClipItem::isItemLocked() const
{
    return !(flags() & (QGraphicsItem::ItemIsSelectable));
}

// virtual
void AbstractClipItem::mousePressEvent(QGraphicsSceneMouseEvent * event)
{
    if (event->modifiers() & Qt::ShiftModifier) {
        // User want to do a rectangle selection, so ignore the event to pass it to the view
        event->ignore();
    } else {
        QGraphicsItem::mousePressEvent(event);
    }
}

// virtual
void AbstractClipItem::mouseReleaseEvent(QGraphicsSceneMouseEvent * event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        // User want to do a rectangle selection, so ignore the event to pass it to the view
        event->ignore();
    } else {
        QGraphicsItem::mouseReleaseEvent(event);
    }
}

void AbstractClipItem::mouseMoveEvent(QGraphicsSceneMouseEvent * event)
{
    if (event->buttons() !=  Qt::LeftButton || event->modifiers() & Qt::ControlModifier) {
        // User want to do a rectangle selection, so ignore the event to pass it to the view
        event->ignore();
    } else {
        QGraphicsItem::mouseMoveEvent(event);
    }
}

int AbstractClipItem::itemHeight()
{
    return 0;
}

int AbstractClipItem::itemOffset()
{
    return 0;
}

void AbstractClipItem::setMainSelectedClip(bool selected)
{
    if (selected == m_isMainSelectedClip) return;
    m_isMainSelectedClip = selected;
    update();
}

bool AbstractClipItem::isMainSelectedClip()
{
    return m_isMainSelectedClip;
}

int AbstractClipItem::trackForPos(int position)
{
    int track = 1;
    if (!scene() || scene()->views().isEmpty()) return track;
    CustomTrackView *view = static_cast<CustomTrackView*>(scene()->views()[0]);
    if (view) {
	track = view->getTrackFromPos(position);
    }
    return track;
}

int AbstractClipItem::posForTrack(int track)
{
    int pos = 0;
    if (!scene() || scene()->views().isEmpty()) return pos;
    CustomTrackView *view = static_cast<CustomTrackView*>(scene()->views()[0]);
    if (view) {
	pos = view->getPositionFromTrack(track) + 1;
    }
    return pos;
}

