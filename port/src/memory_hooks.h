#pragma once
namespace rex::ppc { union Register; }
void Fifa07AssetReserve(rex::ppc::Register& available);
void Fifa07PhysicalBefore(rex::ppc::Register& size, rex::ppc::Register& stack);
void Fifa07PhysicalAfter(rex::ppc::Register& result);
void Fifa07PhysicalDirectBefore(rex::ppc::Register& size);
