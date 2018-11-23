// -*- c++ -*-

/*!
 *
 * Copyright (C) 2018 Jolla Ltd.
 *
 * Contact: Jarkko Lehtoranta <jarkko.lehtoranta@jolla.com>
 * Author: Jarkko Lehtoranta <jarkko.lehtoranta@jolla.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


#ifndef MPRISBLUEZPROXY_H
#define MPRISBLUEZPROXY_H

#include <MprisQt>
#include <Mpris>
#include <MprisController>
#include <MprisManager>
#include <MprisPlayer>

#include <QObject>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QSharedPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>

typedef QMap<QString, QDBusVariant> InterfaceList;
typedef QMap<QString, QVariantMap> AdapterList;
typedef QMap<QDBusObjectPath, AdapterList> ManagedObjectList;

Q_DECLARE_METATYPE(InterfaceList)
Q_DECLARE_METATYPE(AdapterList)
Q_DECLARE_METATYPE(ManagedObjectList)

class MprisPlayer;
class MPRIS_QT_EXPORT MprisBluezProxy : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool useSystemBus READ useSystemBus)

public:
    MprisBluezProxy(QObject* parent = 0);
    ~MprisBluezProxy();

public slots:
    bool useSystemBus() const {
        return true;
    }
    void setCurrentController(QSharedPointer<MprisController> currentController);
    void syncData();

    // Mpris2 Player Interface (Bluez Player)
    void onBluezLoopStatusRequested(Mpris::LoopStatus loopStatus);
    void onBluezRateRequested(double rate);
    void onBluezShuffleRequested(bool shuffle);
    void onBluezVolumeRequested(double volume);
    void onBluezNextRequested();
    void onBluezOpenUriRequested(const QUrl &url);
    void onBluezPauseRequested();
    void onBluezPlayRequested();
    void onBluezPlayPauseRequested();
    void onBluezPreviousRequested();
    void onBluezSeekRequested(qlonglong offset);
    void onBluezSetPositionRequested(const QDBusObjectPath &trackId, qlonglong position);
    void onBluezStopRequested();

    // Mpris2 Root Interface (Controller)
    void identityChanged();

    // Mpris2 Player Interface (Controller)
    void canControlChanged();
    void canGoNextChanged();
    void canGoPreviousChanged();
    void canPauseChanged();
    void canPlayChanged();
    void canSeekChanged();
    void loopStatusChanged();
    void maximumRateChanged();
    void metadataChanged();
    void minimumRateChanged();
    void playbackStatusChanged();
    void positionChanged(qlonglong position);
    void rateChanged();
    void shuffleChanged();
    void volumeChanged();
    void seeked(qlonglong position);

private Q_SLOTS:
    void onBluezAdapterQueryFinished(QDBusPendingCallWatcher *watcher);
    void onBluezAdapterAdded(const QDBusObjectPath &path, const InterfaceList &interfaces);
    void onBluezAdapterRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void onBluezServiceRegistered(const QString &service);
    void onBluezServiceUnregistered(const QString &service);

private:
    QVariantMap generateEmptyMetadata();
    void connectController();
    void disconnectController();
    void bluezSearchAndRegister();
    void bluezRegisterPlayer();
    void bluezUnregisterPlayer();

    QSharedPointer<MprisPlayer> m_bluezPlayer;
    QSharedPointer<MprisController> m_currentController;
    QDBusServiceWatcher *m_bluezWatcher;
    QString m_bluezAdapterServicePath;
    bool m_bluezRegistered;
};

#endif // MPRISBLUEZPROXY_H
