# RCX

Write Ruby extensions in C++20. Inspired by [rice](https://github.com/ruby-rice/rice) and [magnus](https://github.com/matsadler/magnus).

# Usage
## Creating a new extension
In your `extconf.rb` file, add the following:
```ruby
require 'rcx/mkmf/c++20'

create_header
create_makefile('your_ext')
```

# License

RCX is licensed under the terms of the [Boost Software License, Version 1.0](./LICENSE.txt).

