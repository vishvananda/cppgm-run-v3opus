#pragma once

// An inline definition every unit that includes this file also holds.  Only a
// base subobject ever runs it here, so only that entry point is owed.
struct shared_base
{
  shared_base() {}
  virtual int value() const { return 1; }
};

struct shared_derived : shared_base
{
  shared_derived();
  virtual int value() const;
  int held;
};

// A definition written outside its class without `inline` is this unit's
// alone wherever it stands, so both of the ABI's entry points are owed even
// though this file is included.
struct written_base
{
  written_base();
  virtual int here() const { return 2; }
};

struct written_derived : written_base
{
  written_derived();
  virtual int here() const;
  int held;
};
