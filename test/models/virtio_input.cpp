/******************************************************************************
 *                                                                            *
 * Copyright 2020 Jan Henrik Weinstock                                        *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License");            *
 * you may not use this file except in compliance with the License.           *
 * You may obtain a copy of the License at                                    *
 *                                                                            *
 *     http://www.apache.org/licenses/LICENSE-2.0                             *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 *                                                                            *
 ******************************************************************************/

#include "testing.h"

class virtio_rng_stim: public test_base
{
public:
    generic::bus    bus;
    generic::memory mem;
    virtio::mmio    virtio;
    virtio::input   virtio_input;
    master_socket   OUT;
    sc_in<bool>     IRQ;
    sc_signal<bool> irq;

    virtio_rng_stim(const sc_module_name& nm = sc_gen_unique_name("stim")):
        test_base(nm),
        bus("bus"),
        mem("mem", 0x1000),
        virtio("virtio"),
        virtio_input("virtio_input"),
        OUT("OUT"),
        IRQ("IRQ"),
        irq("irq") {

        virtio.VIRTIO_OUT.bind(virtio_input.VIRTIO_IN);

        bus.bind(mem.IN, 0, 0xfff);
        bus.bind(virtio.IN, 0x1000, 0x1fff);

        bus.bind(OUT);
        bus.bind(virtio.OUT);

        virtio.IRQ.bind(irq);
        IRQ.bind(irq);

        bus.CLOCK.bind(CLOCK);
        mem.CLOCK.bind(CLOCK);
        virtio.CLOCK.bind(CLOCK);

        bus.RESET.bind(RESET);
        mem.RESET.bind(RESET);
        virtio.RESET.bind(RESET);
    }

    virtual void run_test() override {
        const u64 INPUT_BASE = 0x1000;

        const u64 INPUT_MAGIC   = INPUT_BASE + 0x00;
        const u64 INPUT_VERSION = INPUT_BASE + 0x04;
        const u64 INPUT_DEVID   = INPUT_BASE + 0x08;
        const u64 INPUT_VQ_SEL  = INPUT_BASE + 0x30;
        const u64 INPUT_VQ_MAX  = INPUT_BASE + 0x34;
        const u64 INPUT_STATUS  = INPUT_BASE + 0x70;

        u32 data;
        ASSERT_OK(OUT.readw(INPUT_MAGIC, data));
        ASSERT_EQ(data, 0x74726976);

        ASSERT_OK(OUT.readw(INPUT_VERSION, data));
        ASSERT_EQ(data, 2);

        ASSERT_OK(OUT.readw(INPUT_DEVID, data));
        ASSERT_EQ(data, virtio::VIRTIO_DEVICE_INPUT);

        ASSERT_OK(OUT.readw(INPUT_STATUS, data));
        ASSERT_EQ(data, 0);

        data = virtio::mmio::VIRTIO_STATUS_ACKNOWLEDGE |
               virtio::mmio::VIRTIO_STATUS_DRIVER |
               virtio::mmio::VIRTIO_STATUS_FEATURES_OK;
        ASSERT_OK(OUT.writew(INPUT_STATUS, data));

        ASSERT_OK(OUT.readw(INPUT_STATUS, data));
        ASSERT_TRUE(data & virtio::mmio::VIRTIO_STATUS_FEATURES_OK);

        data = 0;
        ASSERT_OK(OUT.writew(INPUT_VQ_SEL, data));
        ASSERT_OK(OUT.readw(INPUT_VQ_MAX, data));
        EXPECT_EQ(data, 8);

        data = 1;
        ASSERT_OK(OUT.writew(INPUT_VQ_SEL, data));
        ASSERT_OK(OUT.readw(INPUT_VQ_MAX, data));
        EXPECT_EQ(data, 8);

        data = 2;
        ASSERT_OK(OUT.writew(INPUT_VQ_SEL, data));
        ASSERT_OK(OUT.readw(INPUT_VQ_MAX, data));
        EXPECT_EQ(data, 0);
    }

};

TEST(virtio, rng) {
    virtio_rng_stim stim;
    stim.CLOCK.stub(100 * MHz);
    stim.RESET.stub();

    sc_core::sc_start();
}
