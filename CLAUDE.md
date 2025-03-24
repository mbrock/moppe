# Moppe Development Guidelines

## Build Commands
- Build project: `scons`
- Run map test: `./moppe/map/test`
- Generate height map: `./moppe/map/test [seed]` 
- Run application: `./moppe/app/moppe`

## Code Style Guidelines
- **Namespaces**: Use nested namespaces (`moppe::app`, `moppe::gfx`)
- **Function names**: Use snake_case (`render_directly()`)
- **Member variables**: Prefix with `m_` (`m_width`)
- **Global variables**: Prefix with `global_` (`global_app`)
- **Indentation**: 2 spaces
- **Braces**: Opening brace on same line for functions
- **Line Length**: Keep under 80 characters
- **Includes**: Group in order: 1) Project headers 2) STL 3) External libraries

## Error Handling
- Use exceptions for error conditions
- Catch in main function or event handlers
- Use `std::cerr` for error messages
- Graceful exit on errors with code -1

## C++ Features
- C++11 standard
- RAII for resource management
- Enable compiler warnings (-Wall)