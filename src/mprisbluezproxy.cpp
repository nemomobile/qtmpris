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


#include "mprisbluezproxy.h"

#include "mprismanager.h"

#include "mpriscontroller.h"
#include "mprisplayer.h"

#include <qqmlinfo.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusMessage>
#include <QDBusReply>

#include <QtCore/QSignalMapper>

static const QString mprisObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
static const QString dBusBluezService = QStringLiteral("org.bluez");
static const QString dBusBluezMediaInterface = QStringLiteral("org.bluez.Media1");
static const QString dBusBluezMediaRegisterMethod = QStringLiteral("RegisterPlayer");
static const QString dBusBluezMediaUnregisterMethod = QStringLiteral("UnregisterPlayer");

MprisBluezProxy::MprisBluezProxy(QObject *parent)
    : QObject(parent)
    , m_currentController(NULL)
    , m_bluezRegistered(false)
{
    qmlInfo(this) << "Creating MprisBluezProxy";

    // Create mpris bluez player and make connections for it
    m_bluezPlayer = QSharedPointer<MprisPlayer>(new MprisPlayer(this));

    connect(m_bluezPlayer.data(), &MprisPlayer::loopStatusRequested, this, &MprisBluezProxy::onBluezLoopStatusRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::rateRequested, this, &MprisBluezProxy::onBluezRateRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::shuffleRequested, this, &MprisBluezProxy::onBluezShuffleRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::volumeRequested, this, &MprisBluezProxy::onBluezVolumeRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::nextRequested, this, &MprisBluezProxy::onBluezNextRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::openUriRequested, this, &MprisBluezProxy::onBluezOpenUriRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::pauseRequested, this, &MprisBluezProxy::onBluezPauseRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::playRequested, this, &MprisBluezProxy::onBluezPlayRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::playPauseRequested, this, &MprisBluezProxy::onBluezPlayPauseRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::previousRequested, this, &MprisBluezProxy::onBluezPreviousRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::seekRequested, this, &MprisBluezProxy::onBluezSeekRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::setPositionRequested, this, &MprisBluezProxy::onBluezSetPositionRequested);
    connect(m_bluezPlayer.data(), &MprisPlayer::stopRequested, this, &MprisBluezProxy::onBluezStopRequested);

    syncData();

    // Set up Bluez hooks
    qDBusRegisterMetaType<ManagedObjectList>();
    qDBusRegisterMetaType<AdapterList>();
    qDBusRegisterMetaType<InterfaceList>();

    QDBusConnection connection = QDBusConnection::systemBus();
    if (!connection.isConnected()) {
        qmlInfo(this) << "Failed attempting to connect to DBus system bus";
        return;
    }

    m_bluezWatcher = new QDBusServiceWatcher(dBusBluezService,
                                            connection,
                                            QDBusServiceWatcher::WatchForRegistration \
                                            | QDBusServiceWatcher::WatchForUnregistration,
                                            this);
    connect(m_bluezWatcher, SIGNAL(serviceRegistered(QString)), this, SLOT(onBluezServiceRegistered(QString)));
    connect(m_bluezWatcher, SIGNAL(serviceUnregistered(QString)), this, SLOT(onBluezServiceUnregistered(QString)));

    connection.connect(
                dBusBluezService,
                QStringLiteral("/"),
                QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesAdded"),
                this,
                SLOT(onBluezAdapterAdded(QDBusObjectPath, InterfaceList)));
    connection.connect(
                dBusBluezService,
                QStringLiteral("/"),
                QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesRemoved"),
                this,
                SLOT(onBluezAdapterRemoved(QDBusObjectPath, QStringList)));

    bluezSearchAndRegister();

    qmlInfo(this) << "MprisBluezProxy set up";
}

MprisBluezProxy::~MprisBluezProxy()
{
    bluezUnregisterPlayer();
}

void MprisBluezProxy::setCurrentController(QSharedPointer<MprisController> currentController)
{
    if (m_currentController != currentController) {
        // Remove the old controller
        disconnectController();
        // Switch to the new controller
        m_currentController = currentController;
        connectController();
    }
}

void MprisBluezProxy::syncData()
{
    // Make the proxy player controllable by default
    m_bluezPlayer->setCanControl(true);
    // Tracklist support is not implemented
    m_bluezPlayer->setHasTrackList(false);

    if (!m_currentController.isNull()) {
        // Proxy player active - Get data from the current controller
        m_bluezPlayer->setIdentity(m_currentController->identity());
        m_bluezPlayer->setMetadata(m_currentController->metadata());
        m_bluezPlayer->setPosition(m_currentController->position());
        m_bluezPlayer->setCanGoNext(m_currentController->canGoNext());
        m_bluezPlayer->setCanGoPrevious(m_currentController->canGoPrevious());
        m_bluezPlayer->setCanPause(m_currentController->canPause());
        m_bluezPlayer->setCanPlay(m_currentController->canPlay());
        m_bluezPlayer->setCanSeek(m_currentController->canSeek());
        m_bluezPlayer->setMaximumRate(m_currentController->maximumRate());
        m_bluezPlayer->setMinimumRate(m_currentController->minimumRate());
        m_bluezPlayer->setRate(m_currentController->rate());
        m_bluezPlayer->setLoopStatus(m_currentController->loopStatus());
        m_bluezPlayer->setShuffle(m_currentController->shuffle());
        m_bluezPlayer->setVolume(m_currentController->volume());
        m_bluezPlayer->setPlaybackStatus(m_currentController->playbackStatus());
    } else {
        // Proxy player inactive - Reset to defaults
        m_bluezPlayer->setIdentity(QString());
        m_bluezPlayer->setPlaybackStatus(Mpris::Stopped);
        m_bluezPlayer->setPosition(0);
        m_bluezPlayer->setShuffle(false);
        m_bluezPlayer->setLoopStatus(Mpris::None);
        m_bluezPlayer->setMetadata(generateEmptyMetadata());
        m_bluezPlayer->setCanGoNext(false);
        m_bluezPlayer->setCanGoPrevious(false);
        m_bluezPlayer->setCanPause(false);
        m_bluezPlayer->setCanPlay(false);
        m_bluezPlayer->setCanSeek(false);
        m_bluezPlayer->setShuffle(false);
    }
}

QVariantMap MprisBluezProxy::generateEmptyMetadata()
{
    QVariantMap metadata;

    metadata.insert(Mpris::enumerationToString<Mpris::Metadata>(Mpris::Title), QStringLiteral(""));
    metadata.insert(Mpris::enumerationToString<Mpris::Metadata>(Mpris::Artist), QStringLiteral(""));
    metadata.insert(Mpris::enumerationToString<Mpris::Metadata>(Mpris::Album), QStringLiteral(""));
    metadata.insert(Mpris::enumerationToString<Mpris::Metadata>(Mpris::Genre), QStringLiteral(""));
    metadata.insert(Mpris::enumerationToString<Mpris::Metadata>(Mpris::Length), qlonglong(0));
    metadata.insert(Mpris::enumerationToString<Mpris::Metadata>(Mpris::TrackNumber), qint32(0));

    return metadata;
}

// Mpris2 Player Interface (Bluez player)
void MprisBluezProxy::onBluezLoopStatusRequested(Mpris::LoopStatus loopStatus)
{
    if (!m_currentController.isNull()) {
        m_currentController->setLoopStatus(loopStatus);
    }
}

void MprisBluezProxy::onBluezRateRequested(double rate)
{
    if (!m_currentController.isNull()) {
        m_currentController->setRate(rate);
    }
}

void MprisBluezProxy::onBluezShuffleRequested(bool shuffle)
{
    if (!m_currentController.isNull()) {
        m_currentController->setShuffle(shuffle);
    }
}

void MprisBluezProxy::onBluezVolumeRequested(double volume)
{
    if (!m_currentController.isNull()) {
        m_currentController->setVolume(volume);
    }
}

void MprisBluezProxy::onBluezNextRequested()
{
    if (!m_currentController.isNull()) {
        m_currentController->next();
    }
}

void MprisBluezProxy::onBluezOpenUriRequested(const QUrl &url)
{
    if (!m_currentController.isNull()) {
        m_currentController->openUri(url);
    }
}

void MprisBluezProxy::onBluezPauseRequested()
{
    if (!m_currentController.isNull()) {
        m_currentController->pause();
    }
}

void MprisBluezProxy::onBluezPlayRequested()
{
    if (!m_currentController.isNull()) {
        m_currentController->play();
    }
}

void MprisBluezProxy::onBluezPlayPauseRequested()
{
    if (!m_currentController.isNull()) {
        m_currentController->playPause();
    }
}

void MprisBluezProxy::onBluezPreviousRequested()
{
    if (!m_currentController.isNull()) {
        m_currentController->previous();
    }
}

void MprisBluezProxy::onBluezSeekRequested(qlonglong offset)
{
    if (!m_currentController.isNull()) {
        m_currentController->seek(offset);
    }
}

void MprisBluezProxy::onBluezSetPositionRequested(const QDBusObjectPath &trackId, qlonglong position)
{
    if (!m_currentController.isNull()) {
        m_currentController->setPosition(trackId.path(), position);
    }
}

void MprisBluezProxy::onBluezStopRequested()
{
    if (!m_currentController.isNull()) {
        m_currentController->stop();
    }
}

// Mpris2 Root Interface (Controller)
void MprisBluezProxy::identityChanged() {
    m_bluezPlayer->setIdentity(m_currentController->identity());
}

// Mpris2 Player Interface (Controller)
void MprisBluezProxy::canControlChanged() {
    m_bluezPlayer->setCanControl(m_currentController->canControl());
}

void MprisBluezProxy::canGoNextChanged()
{
    m_bluezPlayer->setCanGoNext(m_currentController->canGoNext());
}

void MprisBluezProxy::canGoPreviousChanged()
{
    m_bluezPlayer->setCanGoPrevious(m_currentController->canGoPrevious());
}

void MprisBluezProxy::canPauseChanged()
{
    m_bluezPlayer->setCanPause(m_currentController->canPause());
}

void MprisBluezProxy::canPlayChanged()
{
    m_bluezPlayer->setCanPlay(m_currentController->canPlay());
}

void MprisBluezProxy::canSeekChanged()
{
    m_bluezPlayer->setCanSeek(m_currentController->canSeek());
}

void MprisBluezProxy::loopStatusChanged()
{
    m_bluezPlayer->setLoopStatus(m_currentController->loopStatus());
}

void MprisBluezProxy::maximumRateChanged()
{
    m_bluezPlayer->setMaximumRate(m_currentController->maximumRate());
}

void MprisBluezProxy::metadataChanged()
{
    qmlInfo(this) << "MprisBluezProxy metadata changed";
    qmlInfo(this) << m_currentController->metadata();
    m_bluezPlayer->setMetadata(m_currentController->metadata());
}

void MprisBluezProxy::minimumRateChanged()
{
    m_bluezPlayer->setMinimumRate(m_currentController->minimumRate());
}

void MprisBluezProxy::positionChanged(qlonglong position)
{
    m_bluezPlayer->setPosition(position);
}

void MprisBluezProxy::playbackStatusChanged()
{
    qmlInfo(this) << "MprisBluezProxy playbackstatus changed";
    m_bluezPlayer->setPlaybackStatus(m_currentController->playbackStatus());
}

void MprisBluezProxy::rateChanged()
{
    m_bluezPlayer->setRate(m_currentController->rate());
}

void MprisBluezProxy::shuffleChanged()
{
    m_bluezPlayer->setShuffle(m_currentController->shuffle());
}

void MprisBluezProxy::volumeChanged()
{
    m_bluezPlayer->setVolume(m_currentController->volume());
}

void MprisBluezProxy::seeked(qlonglong position)
{
    m_bluezPlayer->setPosition(position);
    emit m_bluezPlayer->seeked(position);
}

// Private
void MprisBluezProxy::onBluezAdapterQueryFinished(QDBusPendingCallWatcher *watcher)
{
    qmlInfo(this) << "MprisBluezProxy handling adapter query results...";
    watcher->deleteLater();

    const QDBusPendingReply<ManagedObjectList> &reply = *watcher;

    if (!reply.isValid()) {
        switch (reply.error().type()) {
            case QDBusError::ServiceUnknown:
                qmlInfo(this) << "Bluez is not available";
                break;
            default:
                qmlInfo(this) << "Failed to query bluetooth adapters";
                qmlInfo(this) << reply.error().message();
                break;
        }
    } else {
        ManagedObjectList::const_iterator it;
        const ManagedObjectList &managedObjects = reply.value();

        for (it = managedObjects.constBegin(); it != managedObjects.constEnd(); ++it) {
            QString path = it.key().path();
            const AdapterList &adapters = it.value();
            if (adapters.contains(QStringLiteral("org.bluez.Adapter1")) && adapters.contains(dBusBluezMediaInterface)) {
                if (!m_bluezRegistered) {
                    qmlInfo(this) << "MprisBluezProxy using adapter: " << path;
                    m_bluezAdapterServicePath = path;
                    bluezRegisterPlayer();
                }
                return;
            }
        }
    }
}

void MprisBluezProxy::onBluezAdapterAdded(const QDBusObjectPath &path, const InterfaceList &interfaces)
{
    if (!m_bluezRegistered && interfaces.contains(dBusBluezMediaInterface)) {
        qmlInfo(this) << "MprisBluezProxy adapter added";
        qmlInfo(this) << "MprisBluezProxy using adapter: " << path.path();
        m_bluezAdapterServicePath = path.path();
        bluezRegisterPlayer();
    }
}

void MprisBluezProxy::onBluezAdapterRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    if (m_bluezRegistered && m_bluezAdapterServicePath == path.path() && interfaces.contains(dBusBluezMediaInterface)) {
        qmlInfo(this) << "MprisBluezProxy adapter removed";
        m_bluezRegistered = false;
        m_bluezAdapterServicePath.clear();
        // Registered adapter removed, nothing to do
    }
}

void MprisBluezProxy::onBluezServiceRegistered(const QString &service)
{
    if (service == dBusBluezService) {
        qmlInfo(this) << "MprisBluezProxy bluez service appeared, trying to register the player...";
        bluezSearchAndRegister();
    }
}

void MprisBluezProxy::onBluezServiceUnregistered(const QString &service)
{
    if (service == dBusBluezService) {
        m_bluezRegistered = false;
        m_bluezAdapterServicePath.clear();
        qmlInfo(this) << "MprisBluezProxy bluez service vanished";
        // Bluez is gone, nothing to do
    }
}

void MprisBluezProxy::connectController()
{
    qmlInfo(this) << "MprisBluezProxy connect controller";
    if (!m_currentController.isNull()) {
        // Mpris Root Interface
        connect(m_currentController.data(), &MprisController::identityChanged, this, &MprisBluezProxy::identityChanged);

        // Mpris Player Interface
        connect(m_currentController.data(), &MprisController::canControlChanged, this, &MprisBluezProxy::canControlChanged);
        connect(m_currentController.data(), &MprisController::canGoNextChanged, this, &MprisBluezProxy::canGoNextChanged);
        connect(m_currentController.data(), &MprisController::canGoPreviousChanged, this, &MprisBluezProxy::canGoPreviousChanged);
        connect(m_currentController.data(), &MprisController::canPauseChanged, this, &MprisBluezProxy::canPauseChanged);
        connect(m_currentController.data(), &MprisController::canPlayChanged, this, &MprisBluezProxy::canPlayChanged);
        connect(m_currentController.data(), &MprisController::canSeekChanged, this, &MprisBluezProxy::canSeekChanged);
        connect(m_currentController.data(), &MprisController::loopStatusChanged, this, &MprisBluezProxy::loopStatusChanged);
        connect(m_currentController.data(), &MprisController::maximumRateChanged, this, &MprisBluezProxy::maximumRateChanged);
        connect(m_currentController.data(), &MprisController::metadataChanged, this, &MprisBluezProxy::metadataChanged);
        connect(m_currentController.data(), &MprisController::minimumRateChanged, this, &MprisBluezProxy::minimumRateChanged);
        connect(m_currentController.data(), &MprisController::playbackStatusChanged, this, &MprisBluezProxy::playbackStatusChanged);
        connect(m_currentController.data(), &MprisController::positionChanged, this, &MprisBluezProxy::positionChanged);
        connect(m_currentController.data(), &MprisController::rateChanged, this, &MprisBluezProxy::rateChanged);
        connect(m_currentController.data(), &MprisController::shuffleChanged, this, &MprisBluezProxy::shuffleChanged);
        connect(m_currentController.data(), &MprisController::volumeChanged, this, &MprisBluezProxy::volumeChanged);
        connect(m_currentController.data(), &MprisController::seeked, this, &MprisBluezProxy::seeked);

        // Sync with the proxy player
        syncData();
    }
}

void MprisBluezProxy::disconnectController()
{
    qmlInfo(this) << "MprisBluezProxy disconnect controller";
    if (!m_currentController.isNull()) {
        // Mpris Root Interface
        disconnect(m_currentController.data(), &MprisController::identityChanged, this, &MprisBluezProxy::identityChanged);

        // Mpris Player Interface
        disconnect(m_currentController.data(), &MprisController::canControlChanged, this, &MprisBluezProxy::canControlChanged);
        disconnect(m_currentController.data(), &MprisController::canGoNextChanged, this, &MprisBluezProxy::canGoNextChanged);
        disconnect(m_currentController.data(), &MprisController::canGoPreviousChanged, this, &MprisBluezProxy::canGoPreviousChanged);
        disconnect(m_currentController.data(), &MprisController::canPauseChanged, this, &MprisBluezProxy::canPauseChanged);
        disconnect(m_currentController.data(), &MprisController::canPlayChanged, this, &MprisBluezProxy::canPlayChanged);
        disconnect(m_currentController.data(), &MprisController::canSeekChanged, this, &MprisBluezProxy::canSeekChanged);
        disconnect(m_currentController.data(), &MprisController::loopStatusChanged, this, &MprisBluezProxy::loopStatusChanged);
        disconnect(m_currentController.data(), &MprisController::maximumRateChanged, this, &MprisBluezProxy::maximumRateChanged);
        disconnect(m_currentController.data(), &MprisController::metadataChanged, this, &MprisBluezProxy::metadataChanged);
        disconnect(m_currentController.data(), &MprisController::minimumRateChanged, this, &MprisBluezProxy::minimumRateChanged);
        disconnect(m_currentController.data(), &MprisController::playbackStatusChanged, this, &MprisBluezProxy::playbackStatusChanged);
        disconnect(m_currentController.data(), &MprisController::positionChanged, this, &MprisBluezProxy::positionChanged);
        disconnect(m_currentController.data(), &MprisController::rateChanged, this, &MprisBluezProxy::rateChanged);
        disconnect(m_currentController.data(), &MprisController::shuffleChanged, this, &MprisBluezProxy::shuffleChanged);
        disconnect(m_currentController.data(), &MprisController::volumeChanged, this, &MprisBluezProxy::volumeChanged);
        disconnect(m_currentController.data(), &MprisController::seeked, this, &MprisBluezProxy::seeked);

        // Remove the current controller
        m_currentController.clear();
        // Reset the proxy player
        syncData();
    }
}

void MprisBluezProxy::bluezSearchAndRegister()
{
    qmlInfo(this) << "MprisBluezProxy bluez search and register";
    if (m_bluezRegistered) {
        qmlInfo(this) << "MprisBluezProxy already registered";
        return;
    }

    const QString servicePath = QStringLiteral("/");
    const QString serviceInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");

    QDBusMessage message = QDBusMessage::createMethodCall(
                dBusBluezService,
                servicePath,
                serviceInterface,
                QStringLiteral("GetManagedObjects"));

    QDBusConnection connection = QDBusConnection::systemBus();
    if (!connection.isConnected()) {
        qmlInfo(this) << "Failed attempting to connect to DBus system bus";
        return;
    }

    QDBusPendingCall pendingCall = connection.asyncCall(message);

    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, SIGNAL(finished(QDBusPendingCallWatcher*)),
            this, SLOT(onBluezAdapterQueryFinished(QDBusPendingCallWatcher*)));
}

void MprisBluezProxy::bluezRegisterPlayer()
{
    qmlInfo(this) << "MprisBluezProxy register player";
    QDBusMessage message = QDBusMessage::createMethodCall(
                dBusBluezService,
                m_bluezAdapterServicePath,
                dBusBluezMediaInterface,
                dBusBluezMediaRegisterMethod);

    qmlInfo(this) << m_bluezPlayer->metadata();
    QVariantMap properties;
    properties.insert(QStringLiteral("Identity"), m_bluezPlayer->identity());
    properties.insert(QStringLiteral("Metadata"), m_bluezPlayer->metadata());
    properties.insert(QStringLiteral("Position"), m_bluezPlayer->position());
    properties.insert(QStringLiteral("CanControl"), m_bluezPlayer->canControl());
    properties.insert(QStringLiteral("CanGoNext"), m_bluezPlayer->canGoNext());
    properties.insert(QStringLiteral("CanGoPrevious"), m_bluezPlayer->canGoPrevious());
    properties.insert(QStringLiteral("CanPause"), m_bluezPlayer->canPause());
    properties.insert(QStringLiteral("CanPlay"), m_bluezPlayer->canPlay());
    properties.insert(QStringLiteral("LoopStatus"), Mpris::enumerationToString<Mpris::LoopStatus>(m_bluezPlayer->loopStatus()));
    properties.insert(QStringLiteral("Shuffle"), m_bluezPlayer->shuffle());
    properties.insert(QStringLiteral("PlaybackStatus"), Mpris::enumerationToString<Mpris::PlaybackStatus>(m_bluezPlayer->playbackStatus()));

    qmlInfo(this) << properties;

    message.setArguments(QVariantList()
                << QVariant::fromValue(QDBusObjectPath(mprisObjectPath))
                << properties);

    QDBusConnection connection = QDBusConnection::systemBus();
    if (!connection.send(message)) {
        qmlInfo(this) << "Failed to register player to Bluez with" << m_bluezPlayer->serviceName();
        qmlInfo(this) << connection.lastError().message();
        qmlInfo(this) << qPrintable(QDBusConnection::systemBus().lastError().message());
    } else {
        m_bluezRegistered = true;
    }
}

void MprisBluezProxy::bluezUnregisterPlayer()
{
    qmlInfo(this) << "MprisBluezProxy unregister player";
    QDBusMessage message = QDBusMessage::createMethodCall(
                dBusBluezService,
                m_bluezAdapterServicePath,
                dBusBluezMediaInterface,
                dBusBluezMediaUnregisterMethod);

    message.setArguments(QVariantList()
                << QVariant::fromValue(QDBusObjectPath(mprisObjectPath)));

    QDBusConnection connection = QDBusConnection::systemBus();
    if (!connection.send(message)) {
        qmlInfo(this) << "Failed to unregister player from Bluez with" << m_bluezPlayer->serviceName();
        qmlInfo(this) << connection.lastError().message();
        qmlInfo(this) << qPrintable(QDBusConnection::systemBus().lastError().message());
    } else {
        m_bluezRegistered = false;
    }
}
