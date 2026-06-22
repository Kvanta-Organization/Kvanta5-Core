// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_QT_TRAFFICGRAPHWIDGET_H
#define KVANTA5_QT_TRAFFICGRAPHWIDGET_H

#include <QWidget>
#include <QQueue>

#include <chrono>

class ClientModel;

QT_BEGIN_NAMESPACE
class QPaintEvent;
class QTimer;
QT_END_NAMESPACE

class TrafficGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TrafficGraphWidget(QWidget *parent = nullptr);
    void setClientModel(ClientModel *model);
    std::chrono::minutes getGraphRange() const;

protected:
    void paintEvent(QPaintEvent *) override;

public Q_SLOTS:
    void updateRates();
    void setGraphRange(std::chrono::minutes new_range);
    void clear();

private:
    void paintPath(QPainterPath &path, QQueue<float> &samples);

    QTimer* timer{nullptr};
    float fMax{0.0f};
    std::chrono::minutes m_range{0};
    QQueue<float> vSamplesIn;
    QQueue<float> vSamplesOut;
    quint64 nLastBytesIn{0};
    quint64 nLastBytesOut{0};
    ClientModel* clientModel{nullptr};
};

#endif // KVANTA5_QT_TRAFFICGRAPHWIDGET_H
