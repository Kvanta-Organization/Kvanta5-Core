// Copyright (c) 2015-2026 The Bitcoin Core developers
// Copyright (c) 2026 The Kvanta5 Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KVANTA5_QT_BITCOINADDRESSVALIDATOR_H
#define KVANTA5_QT_BITCOINADDRESSVALIDATOR_H

#include <QValidator>

/** Base58 entry widget validator, checks for valid characters and
 * removes some whitespace.
 */
class Kvanta5AddressEntryValidator : public QValidator
{
    Q_OBJECT

public:
    explicit Kvanta5AddressEntryValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

/** Kvanta5 address widget validator, checks for a valid kvanta5 address.
 */
class Kvanta5AddressCheckValidator : public QValidator
{
    Q_OBJECT

public:
    explicit Kvanta5AddressCheckValidator(QObject *parent);

    State validate(QString &input, int &pos) const override;
};

#endif // KVANTA5_QT_BITCOINADDRESSVALIDATOR_H
