#!/usr/bin/env python3

from ruamel import yaml
import os

def subst_anchors(node):
    # Check if the current node is an alias
    if node.alias:
        # Substitute the alias with the anchor node
        node.subst() 

    # Recurse over all child nodes 
    for child in node.value:
        subst_anchors(child)

def default_main():
  with open(".github/workflows/in/release.in.yml") as in_file:
    yaml_serde = yaml.YAML(typ='rt', pure=True)
    data = yaml_serde.load(in_file)
    subst_anchors(data)
    with open(".github/workflows/release.yml", "w") as out_file:
      yaml_serde.dump(data, out_file)

if __name__ == '__main__':
  default_main()
