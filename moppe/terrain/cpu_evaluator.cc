#include <moppe/terrain/cpu_evaluator.hh>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace moppe::terrain {
  namespace {
    struct LoadConstant { float value; };
    struct LoadX { };
    struct LoadY { };
    struct AddRegisters { std::size_t left, right; };
    struct MultiplyRegisters { std::size_t left, right; };
    struct SineRegister { std::size_t operand; };

    using Instruction = std::variant
      <LoadConstant, LoadX, LoadY, AddRegisters,
       MultiplyRegisters, SineRegister>;

    struct Program {
      std::vector<Instruction> instructions;
      std::size_t output;
    };

    Program compile (const ScalarField& field) {
      Program program;
      program.instructions.reserve (unique_node_count (field));
      std::unordered_map<const expression::Node*, std::size_t> registers;

      std::function<std::size_t (const expression::NodePtr&)> emit;
      emit = [&] (const expression::NodePtr& node) -> std::size_t {
	if (const auto found = registers.find (node.get ());
	    found != registers.end ())
	  return found->second;

	Instruction instruction = std::visit
	  ([&] (const auto& operation) -> Instruction {
	    using T = std::decay_t<decltype (operation)>;
	    if constexpr (std::is_same_v<T, expression::Constant>)
	      return LoadConstant { operation.value };
	    else if constexpr (std::is_same_v<T, expression::CoordinateX>)
	      return LoadX { };
	    else if constexpr (std::is_same_v<T, expression::CoordinateY>)
	      return LoadY { };
	    else if constexpr (std::is_same_v<T, expression::Add>)
	      return AddRegisters
		{ emit (operation.left), emit (operation.right) };
	    else if constexpr (std::is_same_v<T, expression::Multiply>)
	      return MultiplyRegisters
		{ emit (operation.left), emit (operation.right) };
	    else
	      return SineRegister { emit (operation.operand) };
	  }, node->operation);

	const std::size_t index = program.instructions.size ();
	program.instructions.push_back (std::move (instruction));
	registers.emplace (node.get (), index);
	return index;
      };

      program.output = emit (field.node ());
      return program;
    }

    void validate (const Domain2D& domain) {
      if (domain.width < 2 || domain.height < 2)
	throw std::invalid_argument
	  ("a field domain needs at least two samples per axis");
      if (!(domain.max_x > domain.min_x)
	  || !(domain.max_y > domain.min_y))
	throw std::invalid_argument
	  ("a field domain needs increasing coordinate bounds");
      if (domain.width >
	  std::numeric_limits<std::size_t>::max () / domain.height)
	throw std::length_error ("field domain is too large");
    }
  }

  ScalarRaster::ScalarRaster (Domain2D domain,
			      std::vector<float> values)
    : m_domain (domain), m_values (std::move (values)) {
    if (m_values.size () != m_domain.width * m_domain.height)
      throw std::invalid_argument
	("raster value count does not match its domain");
  }

  float ScalarRaster::at (std::size_t x, std::size_t y) const {
    if (x >= m_domain.width || y >= m_domain.height)
      throw std::out_of_range ("raster coordinate is out of range");
    return m_values[y * m_domain.width + x];
  }

  float ScalarRaster::min_value () const {
    return *std::min_element (m_values.begin (), m_values.end ());
  }

  float ScalarRaster::max_value () const {
    return *std::max_element (m_values.begin (), m_values.end ());
  }

  ScalarRaster CpuEvaluator::evaluate
    (const ScalarField& field, const Domain2D& domain) const {
    validate (domain);
    const Program program = compile (field);
    std::vector<float> registers (program.instructions.size ());
    std::vector<float> output (domain.width * domain.height);

    for (std::size_t y = 0; y < domain.height; ++y) {
      const float fy = static_cast<float> (y) / (domain.height - 1);
      const float py = domain.min_y + fy * (domain.max_y - domain.min_y);

      for (std::size_t x = 0; x < domain.width; ++x) {
	const float fx = static_cast<float> (x) / (domain.width - 1);
	const float px = domain.min_x
	  + fx * (domain.max_x - domain.min_x);

	for (std::size_t i = 0; i < program.instructions.size (); ++i) {
	  registers[i] = std::visit ([&] (const auto& instruction) {
	    using T = std::decay_t<decltype (instruction)>;
	    if constexpr (std::is_same_v<T, LoadConstant>)
	      return instruction.value;
	    else if constexpr (std::is_same_v<T, LoadX>)
	      return px;
	    else if constexpr (std::is_same_v<T, LoadY>)
	      return py;
	    else if constexpr (std::is_same_v<T, AddRegisters>)
	      return registers[instruction.left] + registers[instruction.right];
	    else if constexpr (std::is_same_v<T, MultiplyRegisters>)
	      return registers[instruction.left] * registers[instruction.right];
	    else
	      return std::sin (registers[instruction.operand]);
	  }, program.instructions[i]);
	}

	output[y * domain.width + x] = registers[program.output];
      }
    }

    return ScalarRaster (domain, std::move (output));
  }
}
