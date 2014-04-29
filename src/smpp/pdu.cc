/*
 * Copyright (C) 2011 OnlineCity
 * Licensed under the MIT license, which can be read at: http://www.opensource.org/licenses/mit-license.php
 * @author hd@onlinecity.dk & td@onlinecity.dk
 */

#include "smpp/pdu.h"
#include <netinet/in.h>
#include <string>
#include <iostream>
#include "hexdump.h"

using std::ios;
using std::ios_base;
using std::ends;
using std::streamsize;
using std::dec;
using std::hex;
using std::endl;
//using boost::shared_array;

namespace smpp {

PDU::PDU() :
  sb(""), buf(&sb), cmdId(0), cmdStatus(0), seqNo(0), nullTerminateOctetStrings(true), null(true) {
}

PDU::PDU(const uint32_t &_cmdId, const uint32_t &_cmdStatus, const uint32_t &_seqNo) :
  sb(""), buf(&sb), cmdId(_cmdId), cmdStatus(_cmdStatus), seqNo(_seqNo),
  nullTerminateOctetStrings(true), null(
    false) {
  (*this) << uint32_t(0);
  (*this) << cmdId;
  (*this) << cmdStatus;
  (*this) << seqNo;
}

PDU::PDU(const PduLengthHeader &pduLength, const PduData &pduBuffer) :
  sb(""), buf(&sb), cmdId(0), cmdStatus(0), seqNo(0), nullTerminateOctetStrings(true), null(false) {
  uint32_t bufSize = PDU::GetPduLength(pduLength);
  buf.write(pduLength.data(), HEADERFIELD_SIZE);

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write length");
  }

  buf.write(pduBuffer.c_str(), bufSize - HEADERFIELD_SIZE);

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write octets");
  }

  buf.seekg(HEADERFIELD_SIZE, std::ios::cur);

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to skip size header");
  }

  (*this) >> cmdId;
  (*this) >> cmdStatus;
  (*this) >> seqNo;
}

PDU::PDU(const PDU &rhs) :
  sb(rhs.sb.str()), /**/
  buf(&sb), /**/
  cmdId(rhs.cmdId), /**/
  cmdStatus(rhs.cmdStatus), /**/
  seqNo(rhs.seqNo), /**/
  nullTerminateOctetStrings(rhs.nullTerminateOctetStrings), /**/
  null(rhs.null) {
  ResetMarker();  // remember to reset the marker after copying.
}

const PduData PDU::getOctets() {
  uint32_t size = getSize();
  uint32_t beSize = htonl(size);
  buf.seekp(0, ios::beg);
  buf.write(reinterpret_cast<char*>(&beSize), sizeof(uint32_t));

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write length");
  }

  buf.seekp(0, ios::end);
  buf.seekg(0, ios::beg);
  PduData octets;
  octets.resize(size);
  buf.read(&*octets.begin(), size);

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to read octets");
  }

  // Seek to start of PDU body
  ResetMarker();
  return octets;
}

int PDU::getSize() {
  buf.seekp(0, ios_base::end);
  int s = buf.tellp();
  return s;
}

uint32_t PDU::getCommandId() const {
  return cmdId;
}

uint32_t PDU::getCommandStatus() const {
  return cmdStatus;
}

uint32_t PDU::getSequenceNo() const {
  return seqNo;
}

bool PDU::isNullTerminating() const {
  return nullTerminateOctetStrings;
}

void PDU::setNullTerminateOctetStrings(const bool &b) {
  nullTerminateOctetStrings = b;
}

PDU &PDU::operator<<(const int &i) {
  uint8_t x(i);
  buf.write(reinterpret_cast<char*>(&x), sizeof(uint8_t));

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write int");
  }

  return *this;
}

PDU &PDU::operator<<(const uint8_t &i) {
  uint8_t x(i);
  buf.write(reinterpret_cast<char*>(&x), sizeof(uint8_t));

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write uint8_t");
  }

  return *this;
}

PDU &PDU::operator<<(const uint16_t &i) {
  uint16_t j = htons(i);
  buf.write(reinterpret_cast<char*>(&j), sizeof(uint16_t));

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write uint16_t");
  }

  return *this;
}

PDU &PDU::operator<<(const uint32_t &i) {
  uint32_t j = htonl(i);
  buf.write(reinterpret_cast<char*>(&j), sizeof(uint32_t));

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write uint32_t");
  }

  return *this;
}

PDU &PDU::operator<<(const std::basic_string<char> &s) {
  buf.write(s.c_str(), s.length());  // use buf.write to allow for UCS-2 chars which are 16-bit.

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write string");
  }

  if (nullTerminateOctetStrings) {
    buf << ends;
  }

  return *this;
}

PDU &PDU::operator <<(const smpp::SmppAddress s) {
  (*this) << s.ton_;
  (*this) << s.npi_;
  (*this) << s.value_;
  return *this;
}

PDU &PDU::operator <<(smpp::TLV tlv) {
  (*this) << tlv.getTag();
  (*this) << tlv.getLen();

  if (tlv.getLen() != 0) {
    (*this).addOctets(tlv.getOctets(), (uint32_t) tlv.getLen());
  }

  return *this;
}

PDU &PDU::addOctets(const PduData &octets, const streamsize &len) {
  buf.write(octets.c_str(), len);

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to write octets");
  }

  return *this;
}

void PDU::Skip(int octets) {
  buf.seekg(octets, ios_base::cur);

  if (buf.fail()) {
    throw smpp::SmppException(buf.bad() ? "Last PDU IO operation failed" : "PDU seek to invalid pos");
  }
}

void PDU::ResetMarker() {
  // Seek to start of PDU body (after headers)
  buf.seekg(HEADERFIELD_SIZE * 4, ios::beg);

  if (buf.fail()) {
    throw smpp::SmppException("PDU failed to reset marker");
  }
}

PDU &PDU::operator>>(int &i) {
  uint8_t j;
  buf.read(reinterpret_cast<char*>(&j), sizeof(uint8_t));

  if (buf.fail()) {
    throw smpp::SmppException(buf.eof() ? "PDU reached EOF" : "Last PDU IO operation failed");
  }

  i = j;
  return *this;
}

PDU &PDU::operator>>(uint8_t &i) {
  buf.read(reinterpret_cast<char*>(&i), sizeof(uint8_t));

  if (buf.fail()) {
    throw smpp::SmppException(buf.eof() ? "PDU reached EOF" : "Last PDU IO operation failed");
  }

  return *this;
}

PDU &PDU::operator>>(uint16_t &i) {
  buf.read(reinterpret_cast<char*>(&i), sizeof(uint16_t));

  if (buf.fail()) {
    throw smpp::SmppException(buf.eof() ? "PDU reached EOF" : "Last PDU IO operation failed");
  }

  i = ntohs(i);
  return *this;
}

PDU &PDU::operator>>(uint32_t &i) {
  buf.read(reinterpret_cast<char*>(&i), sizeof(uint32_t));

  if (buf.fail()) {
    throw smpp::SmppException(buf.eof() ? "PDU reached EOF" : "Last PDU IO operation failed");
  }

  i = ntohl(i);
  return *this;
}

PDU &PDU::operator>>(std::basic_string<char> &s) {
  getline(buf, s, '\0');
  return *this;
}

void PDU::ReadOctets(PduData *octets, const streamsize &len) {
  octets->resize(len);
  buf.readsome(&*octets->begin(), len);

  if (buf.fail()) {
    throw smpp::SmppException(buf.eof() ? "PDU reached EOF" : "Last PDU IO operation failed");
  }
}

bool PDU::HasMoreData() {
  buf.peek();  // peek sets eof, it's not set until you try to read or peek at the data
  return !buf.eof();
}

uint32_t PDU::GetPduLength(const PduLengthHeader &pduHeader) {
  auto i = reinterpret_cast<const uint32_t*>(pduHeader.data());
  return ntohl(*i);
}

}  // namespace smpp

std::ostream &smpp::operator<<(std::ostream &out, smpp::PDU &pdu) {
  if (pdu.null) {
    out << "PDU IS NULL" << std::endl;
    return out;
  }

  int size = pdu.getSize();
  out << "size      :" << pdu.getSize() << endl << "sequence  :" << pdu.getSequenceNo() << endl <<
      "cmd id    :0x"
      << hex << pdu.getCommandId() << dec << endl << "cmd status:0x" << hex << pdu.getCommandStatus() <<
      dec << " : "
      << smpp::GetEsmeStatus(pdu.getCommandStatus()) << endl;
  out << oc::tools::hexdump(reinterpret_cast<const unsigned char*>(pdu.getOctets().c_str()), static_cast<size_t>(size));
  return out;
}
