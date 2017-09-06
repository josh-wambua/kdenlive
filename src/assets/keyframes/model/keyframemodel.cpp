/***************************************************************************
 *   Copyright (C) 2017 by Nicolas Carion                                  *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3 or any later version accepted by the       *
 *   membership of KDE e.V. (or its successor approved  by the membership  *
 *   of KDE e.V.), which shall act as a proxy defined in Section 14 of     *
 *   version 3 of the license.                                             *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "keyframemodel.hpp"
#include "doc/docundostack.hpp"
#include "core.h"
#include "assets/model/assetparametermodel.hpp"
#include "macros.hpp"

#include <QDebug>
#include <mlt++/Mlt.h>


KeyframeModel::KeyframeModel(std::weak_ptr<AssetParameterModel> model, const QModelIndex &index, std::weak_ptr<DocUndoStack> undo_stack, QObject *parent)
    : QAbstractListModel(parent)
    , m_model(std::move(model))
    , m_undoStack(std::move(undo_stack))
    , m_index(index)
    , m_lock(QReadWriteLock::Recursive)
{
    setup();
    refresh();
}


void KeyframeModel::setup()
{
    // We connect the signals of the abstractitemmodel to a more generic one.
    connect(this, &KeyframeModel::columnsMoved, this, &KeyframeModel::modelChanged);
    connect(this, &KeyframeModel::columnsRemoved, this, &KeyframeModel::modelChanged);
    connect(this, &KeyframeModel::columnsInserted, this, &KeyframeModel::modelChanged);
    connect(this, &KeyframeModel::rowsMoved, this, &KeyframeModel::modelChanged);
    connect(this, &KeyframeModel::rowsRemoved, this, &KeyframeModel::modelChanged);
    connect(this, &KeyframeModel::rowsInserted, this, &KeyframeModel::modelChanged);
    connect(this, &KeyframeModel::modelReset, this, &KeyframeModel::modelChanged);
    connect(this, &KeyframeModel::dataChanged, this, &KeyframeModel::modelChanged);
    connect(this, &KeyframeModel::modelChanged, this, &KeyframeModel::sendModification);
}

bool KeyframeModel::addKeyframe(GenTime pos, KeyframeType type, double value, Fun &undo, Fun &redo)
{
    QWriteLocker locker(&m_lock);
    Fun local_undo = []() { return true; };
    Fun local_redo = []() { return true; };
    if (m_keyframeList.count(pos) > 0) {
        if (std::pair<KeyframeType, double>({type, value}) == m_keyframeList.at(pos)) {
            return true; // nothing to do
        }
        // In this case we simply change the type and value
        KeyframeType oldType = m_keyframeList[pos].first;
        double oldValue = m_keyframeList[pos].second;
        local_undo = updateKeyframe_lambda(pos, oldType, oldValue);
        local_redo = updateKeyframe_lambda(pos, type, value);
    } else {
        local_redo = addKeyframe_lambda(pos, type, value);
        local_undo = deleteKeyframe_lambda(pos);
    }
    if (local_redo()) {
        UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
        return true;
    }
    return false;
}

bool KeyframeModel::addKeyframe(GenTime pos, KeyframeType type, double value)
{
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };

    bool update = (m_keyframeList.count(pos) > 0);
    bool res = addKeyframe(pos, type, value, undo, redo);
    if (res) {
        PUSH_UNDO(undo, redo, update ? i18n("Change keyframe type") : i18n("Add keyframe"));
    }
    return res;
}

bool KeyframeModel::removeKeyframe(GenTime pos, Fun &undo, Fun &redo)
{
    QWriteLocker locker(&m_lock);
    Q_ASSERT(m_keyframeList.count(pos) > 0);
    KeyframeType oldType = m_keyframeList[pos].first;
    double oldValue = m_keyframeList[pos].second;
    Fun local_undo = addKeyframe_lambda(pos, oldType, oldValue);
    Fun local_redo = deleteKeyframe_lambda(pos);
    if (local_redo()) {
        UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
        return true;
    }
    return false;
}

bool KeyframeModel::removeKeyframe(GenTime pos)
{
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };

    if (m_keyframeList.count(pos) > 0 && m_keyframeList.find(pos) == m_keyframeList.begin()) {
        return false;  // initial point must stay
    }

    bool res = removeKeyframe(pos, undo, redo);
    if (res) {
        PUSH_UNDO(undo, redo, i18n("Delete keyframe"));
    }
    return res;
}

bool KeyframeModel::moveKeyframe(GenTime oldPos, GenTime pos, Fun &undo, Fun &redo)
{
    QWriteLocker locker(&m_lock);
    Q_ASSERT(m_keyframeList.count(oldPos) > 0);
    KeyframeType oldType = m_keyframeList[oldPos].first;
    double oldValue = m_keyframeList[pos].second;
    if (oldPos == pos ) return true;
    Fun local_undo = []() { return true; };
    Fun local_redo = []() { return true; };
    bool res = removeKeyframe(oldPos, local_undo, local_redo);
    if (res) {
        res = addKeyframe(pos, oldType, oldValue, local_undo, local_redo);
    }
    if (res) {
        UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    } else {
        bool undone = local_undo();
        Q_ASSERT(undone);
    }
    return res;
}

bool KeyframeModel::moveKeyframe(GenTime oldPos, GenTime pos, bool logUndo)
{
    QWriteLocker locker(&m_lock);
    Q_ASSERT(m_keyframeList.count(oldPos) > 0);
    if (oldPos == pos ) return true;
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool res = moveKeyframe(oldPos, pos, undo, redo);
    if (res && logUndo) {
        PUSH_UNDO(undo, redo, i18n("Move keyframe"));
    }
    return res;
}

bool KeyframeModel::updateKeyframe(GenTime pos, double value, Fun &undo, Fun &redo)
{
    QWriteLocker locker(&m_lock);
    Q_ASSERT(m_keyframeList.count(pos) > 0);
    KeyframeType oldType = m_keyframeList[pos].first;
    double oldValue = m_keyframeList[pos].second;
    if (qAbs(oldValue - value) < 1e-6) return true;
    auto operation = updateKeyframe_lambda(pos, oldType, oldValue);
    auto reverse = updateKeyframe_lambda(pos, oldType, value);
    bool res = operation();
    if (res) {
        UPDATE_UNDO_REDO(operation, reverse, undo, redo);
    }
    return res;
}

bool KeyframeModel::updateKeyframe(GenTime pos, double value)
{
    QWriteLocker locker(&m_lock);
    Q_ASSERT(m_keyframeList.count(pos) > 0);

    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool res = updateKeyframe(pos, value, undo, redo);
    if (res) {
        PUSH_UNDO(undo, redo, i18n("Update keyframe"));
    }
    return res;
}

Fun KeyframeModel::updateKeyframe_lambda(GenTime pos, KeyframeType type, double value)
{
    QWriteLocker locker(&m_lock);
    return [this, pos, type, value]() {
        Q_ASSERT(m_keyframeList.count(pos) > 0);
        int row = static_cast<int>(std::distance(m_keyframeList.begin(), m_keyframeList.find(pos)));
        m_keyframeList[pos].first = type;
        m_keyframeList[pos].second = value;
        emit dataChanged(index(row), index(row), QVector<int>() << TypeRole << ValueRole);
        return true;
    };
}

Fun KeyframeModel::addKeyframe_lambda(GenTime pos, KeyframeType type, double value)
{
    QWriteLocker locker(&m_lock);
    return [this, pos, type, value]() {
        Q_ASSERT(m_keyframeList.count(pos) == 0);
        // We determine the row of the newly added marker
        auto insertionIt = m_keyframeList.lower_bound(pos);
        int insertionRow = static_cast<int>(m_keyframeList.size());
        if (insertionIt != m_keyframeList.end()) {
            insertionRow = static_cast<int>(std::distance(m_keyframeList.begin(), insertionIt));
        }
        beginInsertRows(QModelIndex(), insertionRow, insertionRow);
        m_keyframeList[pos].first = type;
        m_keyframeList[pos].second = value;
        endInsertRows();
        return true;
    };
}

Fun KeyframeModel::deleteKeyframe_lambda(GenTime pos)
{
    QWriteLocker locker(&m_lock);
    return [this, pos]() {
        Q_ASSERT(m_keyframeList.count(pos) > 0);
        Q_ASSERT(pos != GenTime()); // cannot delete initial point
        int row = static_cast<int>(std::distance(m_keyframeList.begin(), m_keyframeList.find(pos)));
        beginRemoveRows(QModelIndex(), row, row);
        m_keyframeList.erase(pos);
        endRemoveRows();
        return true;
    };
}

QHash<int, QByteArray> KeyframeModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[PosRole] = "position";
    roles[FrameRole] = "frame";
    roles[TypeRole] = "type";
    roles[ValueRole] = "value";
    return roles;
}


QVariant KeyframeModel::data(const QModelIndex &index, int role) const
{
    READ_LOCK();
    if (index.row() < 0 || index.row() >= static_cast<int>(m_keyframeList.size()) || !index.isValid()) {
        return QVariant();
    }
    auto it = m_keyframeList.begin();
    std::advance(it, index.row());
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
    case ValueRole:
        return it->second.second;
    case PosRole:
        return it->first.seconds();
    case FrameRole:
    case Qt::UserRole:
        return it->first.frames(pCore->getCurrentFps());
    case TypeRole:
        return QVariant::fromValue<KeyframeType>(it->second.first);
    }
    return QVariant();
}

int KeyframeModel::rowCount(const QModelIndex &parent) const
{
    READ_LOCK();
    if (parent.isValid()) return 0;
    return static_cast<int>(m_keyframeList.size());
}

Keyframe KeyframeModel::getKeyframe(const GenTime &pos, bool *ok) const
{
    READ_LOCK();
    if (m_keyframeList.count(pos) <= 0) {
        // return empty marker
        *ok = false;
        return {GenTime(), KeyframeType::Linear};
    }
    *ok = true;
    return {pos, m_keyframeList.at(pos).first};
}

Keyframe KeyframeModel::getNextKeyframe(const GenTime &pos, bool *ok) const
{
    auto it = m_keyframeList.upper_bound(pos);
    if (it == m_keyframeList.end()) {
        // return empty marker
        *ok = false;
        return {GenTime(), KeyframeType::Linear};
    }
    *ok = true;
    return {(*it).first, (*it).second.first};
}

Keyframe KeyframeModel::getPrevKeyframe(const GenTime &pos, bool *ok) const
{
    auto it = m_keyframeList.lower_bound(pos);
    if (it == m_keyframeList.begin()) {
        // return empty marker
        *ok = false;
        return {GenTime(), KeyframeType::Linear};
    }
    --it;
    *ok = true;
    return {(*it).first, (*it).second.first};
}

Keyframe KeyframeModel::getClosestKeyframe(const GenTime &pos, bool *ok) const
{
    if (m_keyframeList.count(pos) > 0) {
        return getKeyframe(pos, ok);
    }
    bool ok1, ok2;
    auto next = getNextKeyframe(pos, &ok1);
    auto prev = getPrevKeyframe(pos, &ok2);
    *ok = ok1 || ok2;
    if (ok1 && ok2) {
        double fps = pCore->getCurrentFps();
        if (qAbs(next.first.frames(fps) - pos.frames(fps)) < qAbs(prev.first.frames(fps) - pos.frames(fps))) {
            return next;
        }
        return prev;
    } else if (ok1) {
        return next;
    } else if (ok2) {
        return prev;
    }
    // return empty marker
    return {GenTime(), KeyframeType::Linear};
}


bool KeyframeModel::hasKeyframe(int frame) const
{
    return hasKeyframe(GenTime(frame, pCore->getCurrentFps()));
}
bool KeyframeModel::hasKeyframe(const GenTime &pos) const
{
    READ_LOCK();
    return m_keyframeList.count(pos) > 0;
}

bool KeyframeModel::removeAllKeyframes(Fun &undo, Fun &redo)
{
    QWriteLocker locker(&m_lock);
    std::vector<GenTime> all_pos;
    Fun local_undo = []() { return true; };
    Fun local_redo = []() { return true; };
    for (const auto& m : m_keyframeList) {
        all_pos.push_back(m.first);
    }
    bool res = true;
    bool first = true;
    for (const auto& p : all_pos) {
        if (first) { // skip first point
            first = false;
            continue;
        }
        res = removeKeyframe(p, local_undo, local_redo);
        if (!res) {
            bool undone = local_undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    UPDATE_UNDO_REDO(local_redo, local_undo, undo, redo);
    return true;
}

bool KeyframeModel::removeAllKeyframes()
{
    QWriteLocker locker(&m_lock);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    bool res = removeAllKeyframes(undo, redo);
    if (res) {
        PUSH_UNDO(undo, redo, i18n("Delete all keyframes"));
    }
    return res;
}

QString KeyframeModel::getAnimProperty() const
{
    QString prop;
    bool first = true;
    for (const auto keyframe : m_keyframeList) {
        if (first) {
            first = false;
        } else {
            prop += QStringLiteral(";");
        }
        prop += QString::number(keyframe.first.frames(pCore->getCurrentFps()));
        switch (keyframe.second.first) {
        case KeyframeType::Linear:
            prop += QStringLiteral("=");
            break;
        case KeyframeType::Discrete:
            prop += QStringLiteral("|=");
            break;
        case KeyframeType::Curve:
            prop += QStringLiteral("~=");
            break;
        }
        prop += QString::number(keyframe.second.second);
    }
    return prop;
}

mlt_keyframe_type convertToMltType(KeyframeType type)
{
    switch (type) {
    case KeyframeType::Linear:
        return mlt_keyframe_linear;
    case KeyframeType::Discrete:
        return mlt_keyframe_discrete;
    case KeyframeType::Curve:
        return mlt_keyframe_smooth;
    }
    return mlt_keyframe_linear;
}
KeyframeType convertFromMltType(mlt_keyframe_type type)
{
    switch (type) {
    case mlt_keyframe_linear:
        return KeyframeType::Linear;
    case mlt_keyframe_discrete:
        return KeyframeType::Discrete;
    case mlt_keyframe_smooth:
        return KeyframeType::Curve;
    }
    return KeyframeType::Linear;
}

void KeyframeModel::parseAnimProperty(const QString &prop)
{
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };

    Mlt::Properties mlt_prop;
    mlt_prop.set("key", prop.toUtf8().constData());

    Mlt::Animation *anim = mlt_prop.get_anim("key");

    for (int i = 0; i < anim->key_count(); ++i) {
        int frame;
        mlt_keyframe_type type;
        anim->key_get(i, frame, type);
        double value = mlt_prop.anim_get_double("key", frame);
        addKeyframe(GenTime(frame, pCore->getCurrentFps()), convertFromMltType(type), value, undo, redo);
    }

    delete anim;


    /*
    std::vector<std::pair<QString, KeyframeType> > separators({QStringLiteral("="), QStringLiteral("|="), QStringLiteral("~=")});

    QStringList list = prop.split(';', QString::SkipEmptyParts);
    for (const auto& k : list) {
        bool found = false;
        KeyframeType type;
        QStringList values;
        for (const auto &sep : separators) {
            if (k.contains(sep.first)) {
                found = true;
                type = sep.second;
                values = k.split(sep.first);
                break;
            }
        }
        if (!found || values.size() != 2) {
            qDebug() << "ERROR while parsing value of keyframe"<<k<<"in value"<<prop;
            continue;
        }
        QString sep;
        if ()
    }
    */
}


double KeyframeModel::getInterpolatedValue(int p) const
{
    auto pos = GenTime(p, pCore->getCurrentFps());
    return getInterpolatedValue(pos);
}
double KeyframeModel::getInterpolatedValue(const GenTime &pos) const
{
    int p = pos.frames(pCore->getCurrentFps());
    if (m_keyframeList.count(pos) > 0) {
        return m_keyframeList.at(pos).second;
    }
    auto next = m_keyframeList.upper_bound(pos);
    if (next == m_keyframeList.cbegin()) {
        return (m_keyframeList.cbegin())->second.second;
    } else if (next == m_keyframeList.cend()) {
        auto it = m_keyframeList.cend();
        --it;
        return it->second.second;
    }
    auto prev = next;
    --prev;
    // We now have surrounding keyframes, we use mlt to compute the value

    Mlt::Properties prop;
    prop.anim_set("keyframe", prev->second.second, prev->first.frames(pCore->getCurrentFps()), 0, convertToMltType(prev->second.first) );
    prop.anim_set("keyframe", next->second.second, next->first.frames(pCore->getCurrentFps()), 0, convertToMltType(next->second.first) );
    return prop.anim_get_double("keyframe", p);
}

void KeyframeModel::sendModification() const
{
    if (auto ptr = m_model.lock()) {
        Q_ASSERT(m_index.isValid());
        QString name =  ptr->data(m_index, AssetParameterModel::NameRole).toString();
        auto type = ptr->data(m_index, AssetParameterModel::TypeRole).value<ParamType>();
        if (type == ParamType::KeyframeParam) {
            ptr->setParameter(name, getAnimProperty());
        } else {
            Q_ASSERT(false); //Not implemented, TODO
        }
        ptr->dataChanged(m_index, m_index);
    }
}

void KeyframeModel::refresh()
{
    if (auto ptr = m_model.lock()) {
        Q_ASSERT(m_index.isValid());
        auto type = ptr->data(m_index, AssetParameterModel::TypeRole).value<ParamType>();
        if (type == ParamType::KeyframeParam) {
            parseAnimProperty(ptr->data(m_index, AssetParameterModel::ValueRole).toString());
        } else {
            Q_ASSERT(false); //Not implemented, TODO
        }
        ptr->dataChanged(m_index, m_index);
    }
}