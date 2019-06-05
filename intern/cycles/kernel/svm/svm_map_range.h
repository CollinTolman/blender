/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

CCL_NAMESPACE_BEGIN

/* Map Range Node */

ccl_device void svm_node_map_range(KernelGlobals *kg,
                                   ShaderData *sd,
                                   float *stack,
                                   uint value_offset,
                                   uint fromMin_offset,
                                   uint fromMax_offset,
                                   int *offset)
{
  uint4 node1 = read_node(kg, offset);

  float value = stack_load_float(stack, value_offset);
  float fromMin = stack_load_float(stack, fromMin_offset);
  float fromMax = stack_load_float(stack, fromMax_offset);
  float toMin = stack_load_float(stack, node1.y);
  float toMax = stack_load_float(stack, node1.z);

  float r;
  if (fromMax != fromMin && toMax != toMin) {
    r = toMin + ((value - fromMin) / (fromMax - fromMin)) * (toMax - toMin);
  }
  else {
    r = 0.0f;
  }
  stack_store_float(stack, node1.w, r);
}

CCL_NAMESPACE_END
