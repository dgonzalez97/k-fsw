local references = {
  k_fsw_manual = { "Overview", "k_fsw_manual" },
  getting_started = { "Getting started", "getting_started" },
  architecture = { "Architecture", "architecture" },
  zephyr_integration = { "Zephyr integration", "zephyr_integration" },
  communications = { "CSP and links", "communications" },
  services = { "Services and storage", "services" },
  targets = { "Boards and targets", "targets" },
  ground = { "Ground station", "ground" },
  commands = { "Shell commands", "commands" },
  development = { "Contributing", "development" },
  firmware_update = { "Firmware update", "firmware_update" },
  testing = { "Testing", "testing" },
  project_status = { "Project status", "project_status" },
  api_reference = { "generated API reference", nil },
  kfsw_comms = { "generated communications API reference", nil },
  kfsw_services = { "generated services API reference", nil },
}

function Pandoc(document)
  local identifiers = {}
  local chapters = {}
  local chapter = "guide"

  -- Pandoc prefixes IDs with the input filename under --file-scope.
  document = document:walk({ Header = function(header)
    local original = header.identifier
    local short = original:match(".*__(.+)$") or original
    if header.level == 1 then
      chapter = short
      local file = original:match("^(.*)__[^_].*$")
      if file then identifiers[file] = chapter end
      chapters[chapter] = chapter
      header.identifier = chapter
    else
      header.identifier = chapter .. "-" .. short
    end
    identifiers[original] = header.identifier
    return header
  end })

  return document:walk({ Link = function(link)
    local fragment = link.target:match("^#(.+)$")
    if fragment then
      link.target = "#" .. (identifiers[fragment] or fragment)
    elseif not link.target:match("^%a[%w+.-]*:") then
      local folder, anchor = link.target:match("([^/]+)/index%.md#?(.*)$")
      if folder and chapters[folder] then
        link.target = "#" .. folder .. (anchor ~= "" and "-" .. anchor or "")
      end
    end
    return link
  end })
end

local function reference_inline(label, target)
  if target then
    return pandoc.Link(label, "#" .. target)
  end

  return pandoc.Str(label)
end

local function replace_doxygen_references(inlines)
  local output = pandoc.List()
  local index = 1

  while index <= #inlines do
    local marker = inlines[index]
    local separator = inlines[index + 1]
    local identifier = inlines[index + 2]

    if marker and marker.t == "Str"
      and separator and separator.t == "Space"
      and identifier and identifier.t == "Str" then
      if marker.text == "@ref" then
        local name, punctuation = identifier.text:match("^(.-)([.,;:]*)$")
        local reference = references[name]

        if reference then
          output:insert(reference_inline(reference[1], reference[2]))
        else
          output:insert(pandoc.Str(name))
        end

        if punctuation ~= "" then
          output:insert(pandoc.Str(punctuation))
        end

        index = index + 3
        goto continue
      end

      if marker.text == "@subpage" then
        local quote_separator = inlines[index + 3]
        local quoted_title = inlines[index + 4]

        if quote_separator and quote_separator.t == "Space"
          and quoted_title and quoted_title.t == "Quoted" then
          output:insert(pandoc.Link(quoted_title.content, "#" .. identifier.text))
          index = index + 5
          goto continue
        end
      end
    end

    output:insert(marker)
    index = index + 1

    ::continue::
  end

  return output
end

function Para(element)
  if pandoc.utils.stringify(element) == "[TOC]" then
    return {}
  end

  element.content = replace_doxygen_references(element.content)
  return element
end

function Plain(element)
  element.content = replace_doxygen_references(element.content)
  return element
end

function BulletList(element)
  local items = pandoc.List()

  for _, item in ipairs(element.content) do
    local item_text = pandoc.utils.stringify(item)

    if not (item_text:match("API Reference") and item_text:match("public headers")) then
      items:insert(item)
    end
  end

  element.content = items
  return element
end
