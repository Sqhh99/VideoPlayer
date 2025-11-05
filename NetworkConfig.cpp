#include "NetworkConfig.h"
#include <QStringList>

NetworkConfig::NetworkConfig()
    : connectionTimeout(10000)
    , readTimeout(5000)
    , maxRetries(3)
    , retryDelay(1000)
    , bufferSize(1024 * 1024)  // 1MB
    , maxBufferSize(10 * 1024 * 1024)  // 10MB
    , minBufferThreshold(20)
    , maxBufferThreshold(80)
    , userAgent("Qt Video Player")
    , referer("")
    , followRedirects(true)
    , maxRedirects(5)
    , proxyHost("")
    , proxyPort(0)
    , proxyUser("")
    , proxyPassword("")
    , enableQualityControl(true)
    , targetBitrate(2000)  // 2Mbps
    , maxLatency(3000)     // 3秒
{
}

NetworkConfig::NetworkConfig(const NetworkConfig &other)
    : connectionTimeout(other.connectionTimeout)
    , readTimeout(other.readTimeout)
    , maxRetries(other.maxRetries)
    , retryDelay(other.retryDelay)
    , bufferSize(other.bufferSize)
    , maxBufferSize(other.maxBufferSize)
    , minBufferThreshold(other.minBufferThreshold)
    , maxBufferThreshold(other.maxBufferThreshold)
    , userAgent(other.userAgent)
    , referer(other.referer)
    , followRedirects(other.followRedirects)
    , maxRedirects(other.maxRedirects)
    , proxyHost(other.proxyHost)
    , proxyPort(other.proxyPort)
    , proxyUser(other.proxyUser)
    , proxyPassword(other.proxyPassword)
    , enableQualityControl(other.enableQualityControl)
    , targetBitrate(other.targetBitrate)
    , maxLatency(other.maxLatency)
{
}

NetworkConfig& NetworkConfig::operator=(const NetworkConfig &other)
{
    if (this != &other) {
        connectionTimeout = other.connectionTimeout;
        readTimeout = other.readTimeout;
        maxRetries = other.maxRetries;
        retryDelay = other.retryDelay;
        bufferSize = other.bufferSize;
        maxBufferSize = other.maxBufferSize;
        minBufferThreshold = other.minBufferThreshold;
        maxBufferThreshold = other.maxBufferThreshold;
        userAgent = other.userAgent;
        referer = other.referer;
        followRedirects = other.followRedirects;
        maxRedirects = other.maxRedirects;
        proxyHost = other.proxyHost;
        proxyPort = other.proxyPort;
        proxyUser = other.proxyUser;
        proxyPassword = other.proxyPassword;
        enableQualityControl = other.enableQualityControl;
        targetBitrate = other.targetBitrate;
        maxLatency = other.maxLatency;
    }
    return *this;
}

NetworkConfig NetworkConfig::defaultConfig()
{
    return NetworkConfig();
}

bool NetworkConfig::isValid() const
{
    if (connectionTimeout <= 0 || connectionTimeout > 60000) {
        return false;
    }
    
    if (readTimeout <= 0 || readTimeout > 30000) {
        return false;
    }
    
    if (maxRetries < 0 || maxRetries > 10) {
        return false;
    }
    
    if (retryDelay < 0 || retryDelay > 10000) {
        return false;
    }
    
    if (bufferSize <= 0 || bufferSize > maxBufferSize) {
        return false;
    }
    
    if (minBufferThreshold < 0 || minBufferThreshold >= maxBufferThreshold) {
        return false;
    }
    
    if (maxBufferThreshold <= minBufferThreshold || maxBufferThreshold > 100) {
        return false;
    }
    
    if (maxRedirects < 0 || maxRedirects > 20) {
        return false;
    }
    
    if (proxyPort < 0 || proxyPort > 65535) {
        return false;
    }
    
    if (targetBitrate <= 0 || targetBitrate > 100000) {
        return false;
    }
    
    if (maxLatency <= 0 || maxLatency > 60000) {
        return false;
    }
    
    return true;
}

QString NetworkConfig::getErrorString() const
{
    if (!isValid()) {
        return "Invalid network configuration parameters";
    }
    return QString();
}

QString NetworkConfig::toString() const
{
    QStringList parts;
    parts << QString("connectionTimeout=%1").arg(connectionTimeout);
    parts << QString("readTimeout=%1").arg(readTimeout);
    parts << QString("maxRetries=%1").arg(maxRetries);
    parts << QString("retryDelay=%1").arg(retryDelay);
    parts << QString("bufferSize=%1").arg(bufferSize);
    parts << QString("maxBufferSize=%1").arg(maxBufferSize);
    parts << QString("minBufferThreshold=%1").arg(minBufferThreshold);
    parts << QString("maxBufferThreshold=%1").arg(maxBufferThreshold);
    parts << QString("userAgent=%1").arg(userAgent);
    parts << QString("referer=%1").arg(referer);
    parts << QString("followRedirects=%1").arg(followRedirects ? "true" : "false");
    parts << QString("maxRedirects=%1").arg(maxRedirects);
    parts << QString("proxyHost=%1").arg(proxyHost);
    parts << QString("proxyPort=%1").arg(proxyPort);
    parts << QString("proxyUser=%1").arg(proxyUser);
    parts << QString("proxyPassword=%1").arg(proxyPassword);
    parts << QString("enableQualityControl=%1").arg(enableQualityControl ? "true" : "false");
    parts << QString("targetBitrate=%1").arg(targetBitrate);
    parts << QString("maxLatency=%1").arg(maxLatency);
    
    return parts.join(";");
}

bool NetworkConfig::fromString(const QString &configString)
{
    QStringList parts = configString.split(";");
    
    for (const QString &part : parts) {
        QStringList keyValue = part.split("=");
        if (keyValue.size() != 2) {
            continue;
        }
        
        QString key = keyValue[0].trimmed();
        QString value = keyValue[1].trimmed();
        
        if (key == "connectionTimeout") {
            connectionTimeout = value.toInt();
        } else if (key == "readTimeout") {
            readTimeout = value.toInt();
        } else if (key == "maxRetries") {
            maxRetries = value.toInt();
        } else if (key == "retryDelay") {
            retryDelay = value.toInt();
        } else if (key == "bufferSize") {
            bufferSize = value.toInt();
        } else if (key == "maxBufferSize") {
            maxBufferSize = value.toInt();
        } else if (key == "minBufferThreshold") {
            minBufferThreshold = value.toInt();
        } else if (key == "maxBufferThreshold") {
            maxBufferThreshold = value.toInt();
        } else if (key == "userAgent") {
            userAgent = value;
        } else if (key == "referer") {
            referer = value;
        } else if (key == "followRedirects") {
            followRedirects = (value.toLower() == "true");
        } else if (key == "maxRedirects") {
            maxRedirects = value.toInt();
        } else if (key == "proxyHost") {
            proxyHost = value;
        } else if (key == "proxyPort") {
            proxyPort = value.toInt();
        } else if (key == "proxyUser") {
            proxyUser = value;
        } else if (key == "proxyPassword") {
            proxyPassword = value;
        } else if (key == "enableQualityControl") {
            enableQualityControl = (value.toLower() == "true");
        } else if (key == "targetBitrate") {
            targetBitrate = value.toInt();
        } else if (key == "maxLatency") {
            maxLatency = value.toInt();
        }
    }
    
    return isValid();
} 