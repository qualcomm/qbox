/*
 * This file is part of libqemu-cxx
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2015-2019
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <libqemu/libqemu.h>

#include <libqemu-cxx/libqemu-cxx.h>
#include <internals.h>

namespace qemu {

void Device::connect_gpio_out(int idx, Gpio gpio)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);
    QemuGpio* qemu_gpio = reinterpret_cast<QemuGpio*>(gpio.get_qemu_obj());

    m_int->exports().qdev_connect_gpio_out(qemu_dev, idx, qemu_gpio);
}

void Device::connect_gpio_out_named(const char* n, int idx, Gpio gpio)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);
    QemuGpio* qemu_gpio = reinterpret_cast<QemuGpio*>(gpio.get_qemu_obj());

    m_int->exports().qdev_connect_gpio_out_named(qemu_dev, n, idx, qemu_gpio);
}

Gpio Device::get_gpio_in(int idx)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);
    QemuGpio* qemu_gpio = nullptr;

    qemu_gpio = m_int->exports().qdev_get_gpio_in(qemu_dev, idx);
    Object obj(reinterpret_cast<QemuObject*>(qemu_gpio), m_int);

    return Gpio(obj);
}

Gpio Device::get_gpio_in_named(const char* name, int idx)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);
    QemuGpio* qemu_gpio = nullptr;

    qemu_gpio = m_int->exports().qdev_get_gpio_in_named(qemu_dev, name, idx);
    Object obj(reinterpret_cast<QemuObject*>(qemu_gpio), m_int);

    return Gpio(obj);
}

Bus Device::get_child_bus(const char* name)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);
    QemuBus* qemu_bus = nullptr;

    qemu_bus = m_int->exports().qdev_get_child_bus(qemu_dev, name);
    Object obj(reinterpret_cast<QemuObject*>(qemu_bus), m_int);

    return Bus(obj);
}

void Device::set_parent_bus(Bus bus)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);
    QemuBus* qemu_bus = reinterpret_cast<QemuBus*>(bus.get_qemu_obj());

    m_int->exports().qdev_set_parent_bus(qemu_dev, qemu_bus);
}

void Device::set_prop_chardev(const char* name, Chardev chr)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);
    QemuChardev* char_dev = reinterpret_cast<QemuChardev*>(chr.get_qemu_obj());

    m_int->exports().qdev_prop_set_chr(qemu_dev, name, char_dev);
}

void Device::set_prop_string(const char* name, const char* value)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);

    m_int->exports().qdev_prop_set_string(qemu_dev, name, value);
}

void Device::set_prop_uint_array(const char* name, std::vector<unsigned int> vec)
{
    QemuDevice* qemu_dev = reinterpret_cast<QemuDevice*>(m_obj);
    unsigned int* v = &vec[0];

    m_int->exports().qdev_prop_set_uint_array(qemu_dev, name, v, vec.size());
}

} // namespace qemu
