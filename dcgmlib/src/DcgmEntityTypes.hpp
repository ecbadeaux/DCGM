/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <utility>

namespace DcgmNs::EntityTypes
{
/**
 * Helper class to provide std::hash compatible implementation
 * @tparam T - CRTP type derived from BaseHasher
 */
template <class T>
struct BaseHasher
{
    std::size_t operator()(T const &obj) const
    {
        return obj.do_hash();
    }
};

template <class TIdType>
struct BaseId : BaseHasher<BaseId<TIdType>>
{
    using BaseType = BaseId;
    using IdType   = TIdType;

    IdType id {};
    explicit BaseId(IdType value)
        : id(std::move(value))
    {}
    BaseId()                    = default;
    BaseId(BaseId const &other) = default;
    BaseId(BaseId &&) noexcept  = default;

    BaseId &operator=(BaseId const &other) = default;
    BaseId &operator=(BaseId &&) noexcept  = default;

protected:
    friend struct BaseHasher<BaseType>;

    bool operator==(BaseId const &right) const
    {
        return (*this).id == right.id;
    }

    [[nodiscard]] std::size_t do_hash() const
    {
        return std::hash<IdType> {}(id);
    }
};

} // namespace DcgmNs::EntityTypes

namespace DcgmNs::Cpu
{
using DcgmNs::EntityTypes::BaseId;

struct CpuId : BaseId<std::uint64_t>
{
    using BaseType::BaseType;
    using BaseType::operator==;
};

std::ostream &operator<<(std::ostream &os, CpuId const &val);

struct CoreId : BaseId<std::uint64_t>
{
    using BaseType::BaseType;
    using BaseType::operator==;
};

std::ostream &operator<<(std::ostream &os, CoreId const &val);
} // namespace DcgmNs::Cpu

template <>
struct std::hash<DcgmNs::Cpu::CpuId> : DcgmNs::EntityTypes::BaseHasher<DcgmNs::Cpu::CpuId::BaseType>
{};

template <>
struct std::hash<DcgmNs::Cpu::CoreId> : DcgmNs::EntityTypes::BaseHasher<DcgmNs::Cpu::CoreId::BaseType>
{};

namespace DcgmNs::Mig
{
using DcgmNs::EntityTypes::BaseId;

namespace Nvml
{
    struct ComputeInstanceId : BaseId<std::uint32_t>
    {
        using BaseType::BaseType;
        using BaseType::operator==;
    };

    std::ostream &operator<<(std::ostream &os, ComputeInstanceId const &val);

    struct GpuInstanceId : BaseId<std::uint32_t>
    {
        using BaseType::BaseType;
        using BaseType::operator==;
    };

    std::ostream &operator<<(std::ostream &os, GpuInstanceId const &val);
} // namespace Nvml


struct ComputeInstanceId : BaseId<std::uint32_t>
{
    using BaseType::BaseType;
    using BaseType::operator==;
};

std::ostream &operator<<(std::ostream &os, ComputeInstanceId const &val);

struct GpuInstanceProfileId : BaseId<std::uint32_t>
{
    using BaseType::BaseType;
    using BaseType::operator==;
};

struct GpuInstanceId : BaseId<std::uint64_t>
{
    using BaseType::BaseType;
    using BaseType::operator==;
};

std::ostream &operator<<(std::ostream &os, GpuInstanceId const &val);

} // namespace DcgmNs::Mig

template <>
struct std::hash<DcgmNs::Mig::GpuInstanceId> : DcgmNs::EntityTypes::BaseHasher<DcgmNs::Mig::GpuInstanceId::BaseType>
{};

template <>
struct std::hash<DcgmNs::Mig::Nvml::GpuInstanceId>
    : DcgmNs::EntityTypes::BaseHasher<DcgmNs::Mig::Nvml::GpuInstanceId::BaseType>
{};

template <>
struct std::hash<DcgmNs::Mig::Nvml::ComputeInstanceId>
    : DcgmNs::EntityTypes::BaseHasher<DcgmNs::Mig::Nvml::ComputeInstanceId::BaseType>
{};

template <>
struct std::hash<DcgmNs::Mig::ComputeInstanceId>
    : DcgmNs::EntityTypes::BaseHasher<DcgmNs::Mig::ComputeInstanceId::BaseType>
{};
