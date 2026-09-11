/*
 * This file is part of Wireless Display Software for Linux OS
 *
 * Copyright (C) 2021 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <assert.h>
#include <iostream>
#include <string.h>
#include <netinet/in.h> // htons()

#include "information-element.h"

const uint16_t SubelementSizearray[] = {
    9,
    9,
    18,
    24,
    20,
    4,
    10,
    5,
    11,
    3, // variable: 3 + N*24, where N is number of devices connected to GO
    9,
};

Subelement* new_subelement (SubelementId id)
{
    Subelement* element;
    switch (id) {
        case DEVICE_INFORMATION:
            element = (Subelement*)new DeviceInformationSubelement;
            break;
        case ASSOCIATED_BSSID:
            element = (Subelement*)new AssociatedBSSIDSubelement;
            break;
        case COUPLED_SINK_INFORMATION:
            element = (Subelement*)new CoupledSinkInformationSubelement;
            break;
        default:
            element = NULL;
            break;
    }

    if (element) {
        /* Fill in the common values */
        memset(element, 0, SubelementSizearray[id]);
        element->id = id;
        element->length = htons(SubelementSizearray[id] - 3);
    }

    return element;
}

void delete_subelement (Subelement *element)
{
    switch (element->id) {
        case DEVICE_INFORMATION:
            delete ((DeviceInformationSubelement*)element);
            break;
        case ASSOCIATED_BSSID:
            delete ((AssociatedBSSIDSubelement*)element);
            break;
        case COUPLED_SINK_INFORMATION:
            delete ((CoupledSinkInformationSubelement*)element);
            break;
        default:
            assert(false);
    }
}

void InitializeArray(InformationElementArray *infoelementarray, uint len)
{
     infoelementarray->bytes = new uint8_t[len];
     infoelementarray->length = len;
}

void InitializeArrayinBytes(InformationElementArray *infoelementarray, uint len, uint8_t* in_bytes)
{
     infoelementarray->bytes = new uint8_t[len];
     infoelementarray->length = len;
     memcpy (infoelementarray->bytes, in_bytes, len);
}

void DeleteArray(InformationElementArray *infoelementarray)
{
        delete[] infoelementarray->bytes;
}

InformationElement::InformationElement(): length_(0) {}

InformationElement::InformationElement(InformationElementArray* array)
{
    size_t pos = 0;
    length_ = 0;

    /* The bytes come from over-the-air WFD IEs: every field is untrusted.
     * Each subelement starts with a 1-byte id and a 2-byte big-endian
     * length; validate the id, both lengths and every copy against the
     * received buffer, and track length_ as the sum of what was actually
     * stored so serialize() can never overrun its allocation. */
    while (array->length >= pos + 3) {
        uint8_t id_byte = array->bytes[pos];
        size_t declared = ((size_t)array->bytes[pos + 1] << 8) |
                          array->bytes[pos + 2];
        size_t avail = array->length - pos;

        if (3 + declared > avail) {
            /* truncated or corrupt IE, stop parsing */
            break;
        }

        if (id_byte <= ALTERNATIVE_MAC) {
            SubelementId id = (SubelementId)id_byte;
            size_t subelement_size = SubelementSizearray[id];

            if (subelement_size <= avail && subelement_size <= 3 + declared) {
                Subelement *element = new_subelement(id);
                if (element) {
                    memcpy (element, array->bytes + pos, subelement_size);

                    if (subelements_.count(id) && subelements_[id]) {
                        delete_subelement(subelements_[id]);
                        length_ -= SubelementSizearray[id];
                    }

                    subelements_[id] = element;
                    length_ += subelement_size;
                }
            }
        }

        /* advance by the declared wire length, never by zero */
        pos += 3 + declared;
    }
}

InformationElement::~InformationElement()
{
    for (auto it = subelements_.begin(); it != subelements_.end(); it++){
        delete_subelement ((*it).second);
    }
    subelements_.clear();
}

void InformationElement::add_subelement(Subelement* subelement)
{
    SubelementId id = (SubelementId)subelement->id;
    Subelement* old = subelements_[id];
    if (old){
        delete_subelement (old);
    } else {
        length_ += SubelementSizearray[id];
    }
    subelements_[id] = subelement;
}

DeviceType InformationElement::get_device_type() const
{
    auto it = subelements_.find (DEVICE_INFORMATION);
    if (it == subelements_.end()) {
        /* FIXME : exception ? */
        return DUAL_ROLE;
    }

    auto dev_info = (DeviceInformationSubelement*)(*it).second;
    return (DeviceType)dev_info->field1.device_type;
}

int InformationElement::get_rtsp_port() const
{
    auto it = subelements_.find (DEVICE_INFORMATION);
    if (it == subelements_.end()) {
       /* FIXME : exception ? */
       return -1;
    }

    auto dev_info = (DeviceInformationSubelement*)(*it).second;
    return dev_info->session_management_control_port;
}

bool InformationElement::is_session_available() const
{
	bool ret = false;
    auto it = subelements_.find (DEVICE_INFORMATION);
    if (it == subelements_.end()) {
       /* FIXME : exception ? */
       return ret;
    }

    auto dev_info = (DeviceInformationSubelement*)(*it).second;
    ret = dev_info->field1.session_availability ? true : false;
	return ret;
}

bool InformationElement::is_cp_supported() const
{
	bool ret = false;
    auto it = subelements_.find (DEVICE_INFORMATION);
    if (it == subelements_.end()) {
       /* FIXME : exception ? */
       return ret;
    }

    auto dev_info = (DeviceInformationSubelement*)(*it).second;
    ret = dev_info->field2.hdcp_support ? true : false;
	return ret;
}

InformationElementArray* InformationElement::serialize () const
{
    size_t pos = 0;
    size_t total = 0;

    /* Size the buffer from the stored subelements themselves instead of
     * trusting length_ (a uint8_t position also wrapped at 256 before). */
    for (auto it = subelements_.begin(); it != subelements_.end(); it++) {
        total += SubelementSizearray[(*it).second->id];
    }

    InformationElementArray* array = new InformationElementArray();
    InitializeArray(array, total);
    for (auto it = subelements_.begin(); it != subelements_.end(); it++) {
        Subelement* element = (*it).second;
        memcpy (array->bytes + pos, element, SubelementSizearray[element->id]);
        pos += SubelementSizearray[element->id];
    }

    return array;
}

std::string InformationElement::to_string() const
{
    std::string ret;

    InformationElementArray* array = serialize ();

    for (size_t i = 0; i < array->length; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", array->bytes[i]);
        ret += hex;
    }

	DeleteArray(array);
	delete array;

    return ret;
}
