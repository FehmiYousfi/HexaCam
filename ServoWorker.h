#pragma once
#include <QObject>
#include <memory>
#include "servo_client.hpp"

class ServoWorker : public QObject {
  Q_OBJECT
public:
  explicit ServoWorker(std::unique_ptr<ServoControl::ServoClient> servo, QObject* parent=nullptr)
    : QObject(parent), _servo(std::move(servo)) {}

public slots:
  void setPosition(int pos) {
    if (_servo && _servo->isConnected()) {
      _servo->setPosition(pos);
    }
  }

private:
  std::unique_ptr<ServoControl::ServoClient> _servo;
};

