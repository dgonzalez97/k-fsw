local references = {
  commands = { "Command Reference", "commands" },
  development = { "Development", "development" },
  testing = { "Testing", "testing" },
  kfsw_comms = { "generated communications API reference", nil },
  kfsw_services = { "generated services API reference", nil },
}

local current_chapter = "guide"

function Header(element)
  if element.level == 1 then
    current_chapter = element.identifier

    if element.identifier == "k-fsw" then
      element.content = { pandoc.Str("Overview") }
    end
  elseif element.identifier ~= "" then
    element.identifier = current_chapter .. "-" .. element.identifier
  end

  return element
end

function Pandoc(document)
  local blocks = pandoc.List()
  local remove_next_list = false

  for _, block in ipairs(document.blocks) do
    if block.t == "Header" and block.identifier == "k-fsw-documentation" then
      remove_next_list = true
    elseif remove_next_list and block.t == "BulletList" then
      remove_next_list = false
    else
      blocks:insert(block)
    end
  end

  document.blocks = blocks
  return document
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
        local reference = references[identifier.text]

        if reference then
          output:insert(reference_inline(reference[1], reference[2]))
        else
          output:insert(pandoc.Str(identifier.text))
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
